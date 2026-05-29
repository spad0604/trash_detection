from __future__ import annotations

import json
import os
import time
from typing import Any, Dict, Optional
from urllib.parse import urlencode

import rclpy
from rclpy.node import Node
from std_msgs.msg import Empty, Int32MultiArray, String


try:
    import requests
except ImportError:  # pragma: no cover - handled at runtime on the Pi
    requests = None


class FirebaseBridge(Node):
    def __init__(self) -> None:
        super().__init__("firebase_bridge")
        self.declare_parameter(
            "database_url",
            "https://trash-detection-9d793-default-rtdb.firebaseio.com",
        )
        self.declare_parameter("bin_id", "bin_001")
        self.declare_parameter("auth_token", os.getenv("FIREBASE_AUTH_TOKEN", ""))
        self.declare_parameter("full_threshold_percent", 95)
        self.declare_parameter("http_timeout_seconds", 3.0)
        self.declare_parameter("command_poll_interval_seconds", 1.0)

        self.create_subscription(String, "/trash_bin/sensors", self._on_sensors, 10)
        self.create_subscription(Int32MultiArray, "/trash_bin/levels", self._on_levels, 10)
        self.create_subscription(String, "/trash_bin/alerts", self._on_alert, 10)
        self.create_subscription(String, "/trash_bin/classification", self._on_classification, 10)
        self.create_subscription(String, "/trash_bin/state", self._on_state, 10)
        self.create_subscription(String, "/trash_bin/actuator", self._on_actuator, 10)
        self.go_dump_pub = self.create_publisher(Empty, "/trash_bin/go_dump_request", 10)
        self.go_home_pub = self.create_publisher(Empty, "/trash_bin/go_home_request", 10)
        self.command_timer = self.create_timer(
            float(self.get_parameter("command_poll_interval_seconds").value),
            self._poll_commands,
        )

        if requests is None:
            self.get_logger().warn("requests is not installed; Firebase bridge is disabled")

    def _on_sensors(self, msg: String) -> None:
        try:
            data = json.loads(msg.data)
        except json.JSONDecodeError as exc:
            self.get_logger().warn(f"bad sensor JSON: {exc}")
            return

        patch: Dict[str, Any] = {"status": {"last_update": self._now_ms()}}
        sensors: Dict[str, Any] = {}
        levels: Dict[str, Any] = {}
        battery: Dict[str, Any] = {}

        for key, value in data.items():
            if key in {"bin1_percent", "bin2_percent", "bin3_percent"}:
                levels[key] = value
            elif key == "vbat":
                battery["voltage"] = value
                battery["percent"] = self._battery_percent(value)
            elif key != "ir_state":
                sensors[key] = value

        if sensors:
            patch["sensors"] = sensors
        if levels:
            patch["levels"] = levels
            patch["alerts"] = self._level_alerts(levels)
        if battery:
            patch["battery"] = battery

        self._patch_bin(patch)

    def _on_levels(self, msg: Int32MultiArray) -> None:
        if len(msg.data) < 3:
            return
        levels = {
            "bin1_percent": int(msg.data[0]),
            "bin2_percent": int(msg.data[1]),
            "bin3_percent": int(msg.data[2]),
        }
        self._patch_bin(
            {
                "levels": levels,
                "alerts": self._level_alerts(levels),
                "status": {"last_update": self._now_ms()},
            }
        )

    def _on_alert(self, msg: String) -> None:
        alert = msg.data.strip().lower()
        patch = {"status": {"last_update": self._now_ms()}}
        if alert == "fire":
            patch["alerts"] = {"fire_risk": True}
        elif alert == "gas":
            patch["alerts"] = {"gas_leak": True}
        else:
            patch["alerts"] = {alert: True}
        self._patch_bin(patch)

    def _on_classification(self, msg: String) -> None:
        try:
            result = json.loads(msg.data)
        except json.JSONDecodeError:
            result = {"label": msg.data}
        self._patch_bin(
            {
                "status": {
                    "last_classification": str(result.get("label", "unknown")),
                    "last_update": self._now_ms(),
                },
                "classification": result,
            }
        )

    def _on_state(self, msg: String) -> None:
        self._patch_bin({"status": {"state": msg.data.strip(), "last_update": self._now_ms()}})

    def _on_actuator(self, msg: String) -> None:
        try:
            data = json.loads(msg.data)
        except json.JSONDecodeError as exc:
            self.get_logger().warn(f"bad actuator JSON: {exc}")
            return

        raw = data.get("line_raw", [])
        strength = data.get("line_strength", [])
        self._patch_bin(
            {
                "actuator": {
                    "state": data.get("state", "UNKNOWN"),
                    "moving": bool(data.get("moving", False)),
                    "current_bin": int(data.get("current_bin", 0)),
                },
                "navigation": {
                    "line_position": int(data.get("line_position", 0)),
                    "line_active_count": int(data.get("line_active_count", 0)),
                    "line_raw": raw,
                    "line_strength": strength,
                },
                "status": {"last_update": self._now_ms()},
            }
        )

    def _poll_commands(self) -> None:
        if requests is None:
            return
        try:
            timeout = float(self.get_parameter("http_timeout_seconds").value)

            dump_response = requests.get(self._json_url("commands/go_dump"), timeout=timeout)
            if dump_response.status_code >= 300:
                self.get_logger().warn(
                    f"Firebase command GET failed {dump_response.status_code}: {dump_response.text[:160]}"
                )
                return
            if dump_response.json() is True:
                self.get_logger().info("firebase command received: go_dump")
                self.go_dump_pub.publish(Empty())
                self._patch_bin(
                    {
                        "commands": {
                            "go_dump": False,
                            "last_go_dump_request": self._now_ms(),
                        },
                        "status": {"state": "dump_requested", "last_update": self._now_ms()},
                    }
                )

            home_response = requests.get(self._json_url("commands/go_home"), timeout=timeout)
            if home_response.status_code >= 300:
                self.get_logger().warn(
                    f"Firebase command GET failed {home_response.status_code}: {home_response.text[:160]}"
                )
                return
            if home_response.json() is True:
                self.get_logger().info("firebase command received: go_home")
                self.go_home_pub.publish(Empty())
                self._patch_bin(
                    {
                        "commands": {
                            "go_home": False,
                            "last_go_home_request": self._now_ms(),
                        },
                        "status": {"state": "home_requested", "last_update": self._now_ms()},
                    }
                )
        except Exception as exc:
            self.get_logger().warn(f"Firebase command poll error: {exc}")

    def _level_alerts(self, levels: Dict[str, Any]) -> Dict[str, bool]:
        threshold = int(self.get_parameter("full_threshold_percent").value)
        return {
            "bin1_full": int(levels.get("bin1_percent", 0)) >= threshold,
            "bin2_full": int(levels.get("bin2_percent", 0)) >= threshold,
            "bin3_full": int(levels.get("bin3_percent", 0)) >= threshold,
        }

    def _battery_percent(self, voltage: Optional[float]) -> int:
        if voltage is None:
            return 0
        value = max(10.0, min(12.6, float(voltage)))
        return int(round((value - 10.0) / (12.6 - 10.0) * 100))

    def _patch_bin(self, payload: Dict[str, Any]) -> None:
        if requests is None:
            return
        url = self._bin_url()
        timeout = float(self.get_parameter("http_timeout_seconds").value)
        flattened = self._flatten_payload(payload)
        try:
            response = requests.patch(url, json=flattened, timeout=timeout)
            if response.status_code >= 300:
                self.get_logger().warn(f"Firebase PATCH failed {response.status_code}: {response.text[:160]}")
        except Exception as exc:
            self.get_logger().warn(f"Firebase PATCH error: {exc}")

    def _bin_url(self) -> str:
        return self._json_url("")

    def _json_url(self, child_path: str) -> str:
        database_url = str(self.get_parameter("database_url").value).rstrip("/")
        bin_id = str(self.get_parameter("bin_id").value).strip("/")
        auth_token = str(self.get_parameter("auth_token").value)
        child_path = child_path.strip("/")
        suffix = f"/{child_path}" if child_path else ""
        url = f"{database_url}/bins/{bin_id}{suffix}.json"
        if auth_token:
            url = f"{url}?{urlencode({'auth': auth_token})}"
        return url

    @staticmethod
    def _now_ms() -> int:
        return int(time.time() * 1000)

    @staticmethod
    def _flatten_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
        flattened: Dict[str, Any] = {}

        def walk(prefix: str, value: Any) -> None:
            if isinstance(value, dict):
                for child_key, child_value in value.items():
                    child_path = f"{prefix}/{child_key}" if prefix else str(child_key)
                    walk(child_path, child_value)
            else:
                flattened[prefix] = value

        walk("", payload)
        return flattened


def main() -> None:
    rclpy.init()
    node = FirebaseBridge()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
