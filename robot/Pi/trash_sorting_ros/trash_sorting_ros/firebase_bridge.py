from __future__ import annotations

import json
import os
import time
from typing import Any, Dict, Optional
from urllib.parse import urlencode, urlparse, urlunparse, parse_qsl

import rclpy
from rclpy.node import Node
from std_msgs.msg import Empty, Int32MultiArray, String


try:
    import requests
except ImportError:  # pragma: no cover - handled at runtime on the Pi
    requests = None

try:
    import serial
except ImportError:  # pragma: no cover - handled at runtime on the Pi
    serial = None


class FirebaseBridge(Node):
    def __init__(self) -> None:
        super().__init__("firebase_bridge")
        self.declare_parameter(
            "database_url",
            "https://trash-detection-9d793-default-rtdb.firebaseio.com",
        )
        self.declare_parameter("bin_id", "bin_001")
        self.declare_parameter("auth_token", os.getenv("FIREBASE_AUTH_TOKEN", ""))
        self.declare_parameter("full_threshold_percent", 90)
        self.declare_parameter("firebase_update_interval_seconds", 30.0)
        self.declare_parameter("http_timeout_seconds", 3.0)
        self.declare_parameter("command_poll_interval_seconds", 1.0)
        self.declare_parameter("sms_serial_port", "/dev/serial0")
        self.declare_parameter("sms_baudrate", 115200)
        self.declare_parameter("sms_alert_cooldown_seconds", 300.0)
        self.declare_parameter("sim_firebase_enabled", True)
        self.declare_parameter("sim_serial_port", "/dev/serial0")
        self.declare_parameter("sim_baudrate", 115200)
        self.declare_parameter("sim_apn", "v-internet")
        self.declare_parameter("sim_http_timeout_seconds", 30.0)
        self.declare_parameter("sim_retry_interval_seconds", 60.0)

        self.create_subscription(String, "/trash_bin/sensors", self._on_sensors, 10)
        self.create_subscription(Int32MultiArray, "/trash_bin/levels", self._on_levels, 10)
        self.create_subscription(String, "/trash_bin/alerts", self._on_alert, 10)
        self.create_subscription(String, "/trash_bin/classification", self._on_classification, 10)
        self.create_subscription(String, "/trash_bin/state", self._on_state, 10)
        self.create_subscription(String, "/trash_bin/actuator", self._on_actuator, 10)
        self.go_dump_pub = self.create_publisher(Empty, "/trash_bin/go_dump_request", 10)
        self.go_home_pub = self.create_publisher(Empty, "/trash_bin/go_home_request", 10)
        self.stop_pub = self.create_publisher(Empty, "/trash_bin/stop_request", 10)
        self.command_timer = self.create_timer(
            float(self.get_parameter("command_poll_interval_seconds").value),
            self._poll_commands,
        )
        self.update_timer = self.create_timer(
            float(self.get_parameter("firebase_update_interval_seconds").value),
            self._flush_pending_patch,
        )
        self._state = "offline"
        self._last_auto_sms_at: Dict[str, float] = {}
        self._pending_patch: Dict[str, Any] = {}
        self._sim_http_ready = False
        self._last_sim_probe_at = 0.0
        self._last_sim_get_log_at = 0.0
        self._last_sim_commands_body_log_at = 0.0
        self._last_command_snapshot_log_at = 0.0

        if requests is None:
            self.get_logger().warn("requests is not installed; Firebase bridge is disabled")
        if serial is None:
            self.get_logger().warn("pyserial is not installed; SMS sending is disabled")
        self._sim_http_ready = self._probe_sim_http()

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
            level_alerts = self._level_alerts(levels)
            patch["alerts"] = level_alerts
        else:
            level_alerts = {}
        if battery:
            patch["battery"] = battery

        self._patch_bin(patch, immediate=self._has_full_alert(level_alerts))

    def _on_levels(self, msg: Int32MultiArray) -> None:
        if len(msg.data) < 3:
            return
        levels = {
            "bin1_percent": int(msg.data[0]),
            "bin2_percent": int(msg.data[1]),
            "bin3_percent": int(msg.data[2]),
        }
        level_alerts = self._level_alerts(levels)
        self._patch_bin(
            {
                "levels": levels,
                "alerts": level_alerts,
                "status": {"last_update": self._now_ms()},
            },
            immediate=self._has_full_alert(level_alerts),
        )

    def _on_alert(self, msg: String) -> None:
        alert = msg.data.strip().lower()
        patch = {"status": {"last_update": self._now_ms()}}
        if alert == "fire":
            patch["alerts"] = {"fire_risk": True}
            self._send_alert_sms("fire")
        elif alert == "gas":
            patch["alerts"] = {"gas_leak": True}
            self._send_alert_sms("gas")
        else:
            patch["alerts"] = {alert: True}
        self._patch_bin(patch, immediate=True)

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
        self._state = msg.data.strip()
        now_ms = self._now_ms()
        patch: Dict[str, Any] = {"status": {"state": self._state, "last_update": now_ms}}
        if self._state == "dump_completed":
            patch["navigation"] = {
                "dump_completed": True,
                "home_completed": False,
                "last_dump_completed": now_ms,
            }
            patch["commands"] = {"go_dump": False}
        elif self._state == "home_completed":
            patch["navigation"] = {
                "dump_completed": False,
                "home_completed": True,
                "last_home_completed": now_ms,
            }
            patch["commands"] = {"go_home": False}
        elif self._state in {"dump_outbound", "dump_requested"}:
            patch["navigation"] = {
                "dump_completed": False,
                "home_completed": False,
                "last_dump_started": now_ms,
            }
            patch["commands"] = {"go_dump": False}
        elif self._state in {"dump_returning", "home_requested"}:
            patch["navigation"] = {
                "home_completed": False,
                "last_home_started": now_ms,
            }
            patch["commands"] = {"go_home": False}
        self._patch_bin(patch, immediate="commands" in patch)

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
        try:
            commands = self._firebase_get("commands", fallback_if_none=True)
            if not isinstance(commands, dict):
                self._log_command_snapshot(commands)
                commands = {
                    "stop": self._firebase_get("commands/stop", fallback_if_none=True),
                    "go_dump": self._firebase_get("commands/go_dump", fallback_if_none=True),
                    "go_home": self._firebase_get("commands/go_home", fallback_if_none=True),
                    "send_sms": self._firebase_get("commands/send_sms", fallback_if_none=True),
                }
            self._log_command_snapshot(commands)

            active_commands = {
                key: value
                for key, value in commands.items()
                if key in {"stop", "go_dump", "go_home", "send_sms"} and value
            }
            if active_commands:
                self.get_logger().info(f"firebase commands payload: {active_commands}")

            if self._is_true_command(commands.get("stop")):
                self.get_logger().info("firebase command received: stop")
                self.stop_pub.publish(Empty())
                self._patch_bin(
                    {
                        "commands": {
                            "stop": False,
                            "go_dump": False,
                            "go_home": False,
                            "last_stop_request": self._now_ms(),
                        },
                        "status": {"state": "stop_requested", "last_update": self._now_ms()},
                    },
                    immediate=True,
                )
                return

            if self._is_true_command(commands.get("go_dump")):
                if self._can_go_dump():
                    self.get_logger().info("firebase command received: go_dump")
                    self.go_dump_pub.publish(Empty())
                    self._patch_bin(
                        {
                            "commands": {
                                "go_dump": False,
                                "last_go_dump_request": self._now_ms(),
                            }
                        },
                        immediate=True,
                    )
                else:
                    self.get_logger().warn(f"ignored go_dump command while state={self._state}")
                    self._patch_bin({"commands": {"go_dump": False}}, immediate=True)

            if self._is_true_command(commands.get("go_home")):
                if self._can_go_home():
                    self.get_logger().info("firebase command received: go_home")
                    self.go_home_pub.publish(Empty())
                    self._patch_bin(
                        {
                            "commands": {
                                "go_home": False,
                                "last_go_home_request": self._now_ms(),
                            }
                        },
                        immediate=True,
                    )
                else:
                    self.get_logger().warn(f"ignored go_home command while state={self._state}")
                    self._patch_bin({"commands": {"go_home": False}}, immediate=True)

            sms_command = commands.get("send_sms")
            if sms_command:
                self._handle_manual_sms_command(sms_command)
        except Exception as exc:
            self.get_logger().warn(f"Firebase command poll error: {exc}")

    def _log_command_snapshot(self, commands: Any) -> None:
        now = time.time()
        if now - self._last_command_snapshot_log_at < 10.0:
            return
        self._last_command_snapshot_log_at = now
        self.get_logger().info(f"firebase commands snapshot: {commands!r}")

    def _can_go_dump(self) -> bool:
        return self._state not in {"capturing", "sorting", "dump_outbound", "dump_returning", "bin_full"}

    def _can_go_home(self) -> bool:
        return self._state in {
            "idle",
            "awaiting_return",
            "dump_completed",
            "dump_outbound",
            "arrived",
            "home_completed",
            "line_lost",
        }

    def _level_alerts(self, levels: Dict[str, Any]) -> Dict[str, bool]:
        threshold = int(self.get_parameter("full_threshold_percent").value)
        return {
            "bin1_full": int(levels.get("bin1_percent", 0)) >= threshold,
            "bin2_full": int(levels.get("bin2_percent", 0)) >= threshold,
            "bin3_full": int(levels.get("bin3_percent", 0)) >= threshold,
        }

    @staticmethod
    def _has_full_alert(alerts: Dict[str, Any]) -> bool:
        return any(bool(value) for key, value in alerts.items() if key.endswith("_full"))

    @staticmethod
    def _is_true_command(value: Any) -> bool:
        if value is True:
            return True
        if isinstance(value, (int, float)):
            return value == 1
        if isinstance(value, str):
            return value.strip().lower() in {"true", "1", "yes", "on"}
        return False

    def _battery_percent(self, voltage: Optional[float]) -> int:
        if voltage is None:
            return 0
        value = max(10.0, min(12.6, float(voltage)))
        return int(round((value - 10.0) / (12.6 - 10.0) * 100))

    def _send_alert_sms(self, alert_type: str) -> None:
        cooldown = max(0.0, float(self.get_parameter("sms_alert_cooldown_seconds").value))
        now = time.time()
        if now - self._last_auto_sms_at.get(alert_type, 0.0) < cooldown:
            return
        self._last_auto_sms_at[alert_type] = now
        message = self._sms_message(alert_type, manual=False)
        self._send_sms_to_configured_phone(message, f"auto_{alert_type}")

    def _handle_manual_sms_command(self, command: Any) -> None:
        if command is True:
            sms_type = "status"
        elif isinstance(command, str):
            sms_type = command.strip().lower() or "status"
        elif isinstance(command, dict):
            sms_type = str(command.get("type", "status")).strip().lower()
        else:
            sms_type = "status"

        message = self._sms_message(sms_type, manual=True)
        self._send_sms_to_configured_phone(message, f"manual_{sms_type}")
        self._patch_bin({"commands": {"send_sms": False, "last_sms_request": self._now_ms()}}, immediate=True)

    def _send_sms_to_configured_phone(self, message: str, reason: str) -> bool:
        phone = self._get_notification_phone()
        if not phone:
            self.get_logger().warn(f"cannot send SMS for {reason}: notifications/phone_number is empty")
            self._patch_bin(
                {
                    "notifications": {
                        "sms_last_status": "missing_phone",
                        "sms_last_reason": reason,
                        "sms_last_update": self._now_ms(),
                    }
                },
                immediate=True,
            )
            return False

        ok = self._send_sms(phone, message)
        self._patch_bin(
            {
                "notifications": {
                    "sms_last_status": "sent" if ok else "failed",
                    "sms_last_reason": reason,
                    "sms_last_message": message,
                    "sms_last_sent_at": self._now_ms() if ok else None,
                    "sms_last_update": self._now_ms(),
                }
            },
            immediate=True,
        )
        return ok

    def _get_notification_phone(self) -> str:
        return self._normalize_phone(self._firebase_get("notifications/phone_number"))

    @staticmethod
    def _normalize_phone(value: Any) -> str:
        if value is None:
            return ""
        text = str(value).strip()
        if text.startswith("+"):
            return "+" + "".join(ch for ch in text[1:] if ch.isdigit())
        return "".join(ch for ch in text if ch.isdigit())

    def _send_sms(self, phone: str, message: str) -> bool:
        if serial is None:
            return False
        port = str(self.get_parameter("sms_serial_port").value)
        baudrate = int(self.get_parameter("sms_baudrate").value)
        try:
            with serial.Serial(port, baudrate, timeout=1, write_timeout=2) as modem:
                time.sleep(0.2)
                self._write_at(modem, "AT")
                self._write_at(modem, "AT+CMGF=1")
                self._write_at(modem, 'AT+CSCS="GSM"')
                self._write_at(modem, f'AT+CMGS="{phone}"')
                modem.write(message.encode("ascii", errors="replace") + b"\x1A")
                modem.flush()
                time.sleep(4.0)
            self.get_logger().info(f"sent SMS to {phone}: {message}")
            return True
        except Exception as exc:
            self.get_logger().warn(f"SMS send failed on {port}: {exc}")
            return False

    @staticmethod
    def _write_at(modem: Any, command: str) -> None:
        modem.write((command + "\r").encode("ascii"))
        modem.flush()
        time.sleep(0.4)

    def _sms_message(self, sms_type: str, manual: bool) -> str:
        prefix = "Thong bao thu cong" if manual else "Canh bao tu dong"
        if sms_type == "fire":
            return f"{prefix}: Phat hien nguy co chay tai thung rac thong minh."
        if sms_type == "gas":
            return f"{prefix}: Phat hien ro ri khi gas tai thung rac thong minh."
        if sms_type == "full":
            return f"{prefix}: Mot hoac nhieu ngan rac da day, can di do rac."
        return f"{prefix}: He thong thung rac thong minh can duoc kiem tra."

    def _patch_bin(self, payload: Dict[str, Any], *, immediate: bool = False) -> None:
        if requests is None and not self._sim_http_ready:
            return
        flattened = self._flatten_payload(payload)
        if immediate:
            for key in flattened:
                self._pending_patch.pop(key, None)
            self._send_flattened_patch(flattened)
            return
        self._pending_patch.update(flattened)

    def _flush_pending_patch(self) -> None:
        if not self._pending_patch:
            return
        payload = dict(self._pending_patch)
        self._pending_patch.clear()
        self._send_flattened_patch(payload)

    def _send_flattened_patch(self, flattened: Dict[str, Any]) -> None:
        if not flattened:
            return
        url = self._bin_url()
        if self._ensure_sim_http_ready() and self._patch_via_sim(url, flattened):
            return
        if requests is None:
            self.get_logger().warn("requests is not installed and SIM HTTP failed; Firebase PATCH dropped")
            return
        timeout = float(self.get_parameter("http_timeout_seconds").value)
        try:
            response = requests.patch(url, json=flattened, timeout=timeout)
            if response.status_code >= 300:
                self.get_logger().warn(f"Firebase PATCH failed {response.status_code}: {response.text[:160]}")
            else:
                self.get_logger().info(f"Firebase PATCH via normal network ok ({response.status_code})")
        except Exception as exc:
            self.get_logger().warn(f"Firebase PATCH error: {exc}")

    def _firebase_get(self, child_path: str, *, fallback_if_none: bool = False) -> Any:
        url = self._json_url(child_path)
        if self._ensure_sim_http_ready():
            ok, value = self._get_via_sim(url)
            if ok and (value is not None or not fallback_if_none):
                return value
        if requests is None:
            self.get_logger().warn("requests is not installed and SIM HTTP failed; Firebase GET dropped")
            return None
        try:
            timeout = float(self.get_parameter("http_timeout_seconds").value)
            response = requests.get(url, timeout=timeout)
            if response.status_code >= 300:
                self.get_logger().warn(f"Firebase GET failed {response.status_code}: {response.text[:160]}")
                return None
            return response.json()
        except Exception as exc:
            self.get_logger().warn(f"Firebase GET error: {exc}")
            return None

    def _probe_sim_http(self) -> bool:
        if serial is None or not bool(self.get_parameter("sim_firebase_enabled").value):
            return False
        self._last_sim_probe_at = time.time()
        port = str(self.get_parameter("sim_serial_port").value)
        baudrate = int(self.get_parameter("sim_baudrate").value)
        apn = str(self.get_parameter("sim_apn").value)
        try:
            with serial.Serial(port, baudrate, timeout=1, write_timeout=2) as modem:
                time.sleep(0.2)
                if not self._at_ok(modem, "AT"):
                    self.get_logger().warn(f"SIM modem not responding on {port}; Firebase uses normal network")
                    return False
                self._at_ok(modem, "AT+CMEE=2")
                cpin = self._at(modem, "AT+CPIN?", timeout=2)
                if "READY" not in cpin:
                    self.get_logger().warn(f"SIM not ready: {cpin.strip()[:120]}")
                    return False
                self._at(modem, "AT+CSQ", timeout=2)
                self._at(modem, "AT+CEREG?", timeout=2)
                if apn:
                    self._at_ok(modem, f'AT+CGDCONT=1,"IP","{apn}"', timeout=3)
                self._at_ok(modem, "AT+CGACT=1,1", timeout=10)
                cgatt = self._at(modem, "AT+CGATT?", timeout=3)
                if "+CGATT: 1" not in cgatt:
                    self.get_logger().warn(f"SIM packet data is not attached: {cgatt.strip()[:120]}")
                    return False
                self.get_logger().info(f"SIM data ready on {port}; Firebase PATCH will prefer SIM HTTP")
                return True
        except Exception as exc:
            self.get_logger().warn(f"SIM probe failed on {port}: {exc}")
            return False

    def _ensure_sim_http_ready(self) -> bool:
        if self._sim_http_ready:
            return True
        retry_seconds = float(self.get_parameter("sim_retry_interval_seconds").value)
        if retry_seconds <= 0.0:
            return False
        if time.time() - self._last_sim_probe_at < retry_seconds:
            return False
        self._sim_http_ready = self._probe_sim_http()
        return self._sim_http_ready

    def _patch_via_sim(self, url: str, flattened: Dict[str, Any]) -> bool:
        port = str(self.get_parameter("sim_serial_port").value)
        baudrate = int(self.get_parameter("sim_baudrate").value)
        timeout = float(self.get_parameter("sim_http_timeout_seconds").value)
        payload = json.dumps(flattened, separators=(",", ":"), ensure_ascii=True)
        post_url = self._method_override_url(url, "PATCH")
        try:
            with serial.Serial(port, baudrate, timeout=1, write_timeout=5) as modem:
                time.sleep(0.1)
                self._at(modem, "AT+HTTPTERM", timeout=2)
                if not self._at_ok(modem, "AT+HTTPINIT", timeout=5):
                    return self._sim_patch_failed("HTTPINIT failed")
                if urlparse(post_url).scheme == "https":
                    self._at_ok(modem, "AT+HTTPSSL=1", timeout=3)
                else:
                    self._at_ok(modem, "AT+HTTPSSL=0", timeout=3)
                self._at_ok(modem, 'AT+HTTPPARA="CID",1', timeout=3)
                self._at_ok(modem, 'AT+HTTPPARA="CONTENT","application/json"', timeout=3)
                self._at_ok(modem, 'AT+HTTPPARA="USERDATA","X-HTTP-Method-Override: PATCH"', timeout=3)
                if not self._at_ok(modem, f'AT+HTTPPARA="URL","{post_url}"', timeout=5):
                    return self._sim_patch_failed("HTTPPARA URL failed")
                if not self._http_data(modem, payload, timeout=10):
                    return self._sim_patch_failed("HTTPDATA failed")
                response = self._at(modem, "AT+HTTPACTION=1", timeout=timeout, wait_for="+HTTPACTION:")
                self._at(modem, "AT+HTTPTERM", timeout=3)
            status = self._parse_http_status(response)
            if status is not None and 200 <= status < 300:
                self.get_logger().info(f"Firebase PATCH via SIM HTTP ok ({status})")
                return True
            return self._sim_patch_failed(f"HTTPACTION status={status}, response={response.strip()[:160]!r}")
        except Exception as exc:
            return self._sim_patch_failed(f"SIM HTTP exception: {exc}")

    def _get_via_sim(self, url: str) -> tuple[bool, Any]:
        port = str(self.get_parameter("sim_serial_port").value)
        baudrate = int(self.get_parameter("sim_baudrate").value)
        timeout = float(self.get_parameter("sim_http_timeout_seconds").value)
        try:
            with serial.Serial(port, baudrate, timeout=1, write_timeout=5) as modem:
                time.sleep(0.1)
                self._at(modem, "AT+HTTPTERM", timeout=2)
                if not self._at_ok(modem, "AT+HTTPINIT", timeout=5):
                    return self._sim_get_failed("HTTPINIT failed")
                if urlparse(url).scheme == "https":
                    self._at_ok(modem, "AT+HTTPSSL=1", timeout=3)
                else:
                    self._at_ok(modem, "AT+HTTPSSL=0", timeout=3)
                self._at_ok(modem, 'AT+HTTPPARA="CID",1', timeout=3)
                if not self._at_ok(modem, f'AT+HTTPPARA="URL","{url}"', timeout=5):
                    return self._sim_get_failed("HTTPPARA URL failed")
                action = self._at(modem, "AT+HTTPACTION=0", timeout=timeout, wait_for="+HTTPACTION:")
                status = self._parse_http_status(action)
                if status is None or not 200 <= status < 300:
                    self._at(modem, "AT+HTTPTERM", timeout=3)
                    return self._sim_get_failed(f"HTTPACTION status={status}, response={action.strip()[:160]!r}")
                body_response = self._at(modem, "AT+HTTPREAD", timeout=timeout, wait_for="OK")
                self._at(modem, "AT+HTTPTERM", timeout=3)
            body = self._parse_httpread_body(body_response)
            if "/commands" in url:
                now = time.time()
                if now - self._last_sim_commands_body_log_at >= 10.0:
                    self._last_sim_commands_body_log_at = now
                    self.get_logger().info(f"SIM commands GET body: {body[:240]!r}")
            now = time.time()
            if now - self._last_sim_get_log_at >= 30.0:
                self._last_sim_get_log_at = now
                self.get_logger().info(f"Firebase GET via SIM HTTP ok ({status})")
            return True, json.loads(body) if body else None
        except Exception as exc:
            return self._sim_get_failed(f"SIM HTTP GET exception: {exc}")

    def _sim_get_failed(self, reason: str) -> tuple[bool, None]:
        self.get_logger().warn(f"{reason}; falling back to normal Firebase GET")
        self._sim_http_ready = False
        return False, None

    def _sim_patch_failed(self, reason: str) -> bool:
        self.get_logger().warn(f"{reason}; falling back to normal Firebase PATCH")
        self._sim_http_ready = False
        return False

    @staticmethod
    def _method_override_url(url: str, method: str) -> str:
        parsed = urlparse(url)
        query = dict(parse_qsl(parsed.query, keep_blank_values=True))
        query["x-http-method-override"] = method
        return urlunparse(parsed._replace(query=urlencode(query)))

    def _http_data(self, modem: Any, payload: str, timeout: float) -> bool:
        data = payload.encode("utf-8")
        response = self._at(modem, f"AT+HTTPDATA={len(data)},10000", timeout=timeout, wait_for="DOWNLOAD")
        if "DOWNLOAD" not in response:
            return False
        modem.write(data)
        modem.flush()
        response = self._read_until(modem, timeout, {"OK", "ERROR"})
        return "OK" in response and "ERROR" not in response

    def _at_ok(self, modem: Any, command: str, timeout: float = 2.0) -> bool:
        response = self._at(modem, command, timeout=timeout)
        return "OK" in response and "ERROR" not in response

    def _at(self, modem: Any, command: str, timeout: float = 2.0, wait_for: str = "OK") -> str:
        modem.reset_input_buffer()
        modem.write((command + "\r").encode("ascii"))
        modem.flush()
        targets = {wait_for, "ERROR"} if wait_for else {"OK", "ERROR"}
        return self._read_until(modem, timeout, targets)

    @staticmethod
    def _read_until(modem: Any, timeout: float, targets: set[str]) -> str:
        deadline = time.time() + timeout
        chunks = []
        while time.time() < deadline:
            waiting = modem.in_waiting
            data = modem.read(waiting or 1)
            if data:
                text = data.decode("utf-8", errors="replace")
                chunks.append(text)
                response = "".join(chunks)
                if FirebaseBridge._response_has_target(response, targets):
                    return response
            else:
                time.sleep(0.05)
        return "".join(chunks)

    @staticmethod
    def _response_has_target(response: str, targets: set[str]) -> bool:
        for target in targets:
            if target == "+HTTPACTION:":
                for line in response.splitlines():
                    line = line.strip()
                    if not line.startswith("+HTTPACTION:"):
                        continue
                    parts = [part.strip() for part in line.split(":", 1)[1].split(",")]
                    if len(parts) >= 3 and parts[1].isdigit():
                        return True
                continue
            if target in response:
                return True
        return False

    @staticmethod
    def _parse_http_status(response: str) -> Optional[int]:
        for line in response.splitlines():
            line = line.strip()
            if not line.startswith("+HTTPACTION:"):
                continue
            parts = [part.strip() for part in line.split(":", 1)[1].split(",")]
            if len(parts) >= 2:
                try:
                    return int(parts[1])
                except ValueError:
                    return None
        return None

    @staticmethod
    def _parse_httpread_body(response: str) -> str:
        marker = "+HTTPREAD:"
        index = response.find(marker)
        if index < 0:
            return ""
        after_marker = response[index:].splitlines()
        if len(after_marker) < 2:
            return ""
        body_lines = []
        for line in after_marker[1:]:
            stripped = line.strip()
            if stripped in {"OK", "ERROR"} or stripped.startswith("+HTTP"):
                break
            body_lines.append(line)
        return "\n".join(body_lines).strip()

    def _bin_url(self) -> str:
        return self._json_url("")

    def _json_url(self, child_path: str, *, cache_bust: bool = False) -> str:
        database_url = str(self.get_parameter("database_url").value).rstrip("/")
        bin_id = str(self.get_parameter("bin_id").value).strip("/")
        auth_token = str(self.get_parameter("auth_token").value)
        child_path = child_path.strip("/")
        suffix = f"/{child_path}" if child_path else ""
        url = f"{database_url}/bins/{bin_id}{suffix}.json"
        query = {}
        if auth_token:
            query["auth"] = auth_token
        if cache_bust:
            query["_"] = str(self._now_ms())
        if query:
            url = f"{url}?{urlencode(query)}"
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
