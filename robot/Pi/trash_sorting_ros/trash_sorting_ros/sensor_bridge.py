from __future__ import annotations

import time
from typing import Optional

import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Int32MultiArray, String

from .protocol import (
    json_dumps,
    parse_key_value_line,
    parse_levels_payload,
    parse_sensor_payload,
)
from .serial_node import SerialWorker


class SensorBridge(Node):
    def __init__(self) -> None:
        super().__init__("sensor_bridge")
        self.declare_parameter(
            "port",
            "/dev/serial/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.3:1.0-port0",
        )
        self.declare_parameter("baudrate", 115200)
        self.declare_parameter("read_timeout", 0.2)
        self.declare_parameter("reconnect_delay", 2.0)
        self.declare_parameter("poll_interval", 3.0)
        self.declare_parameter("ir_detected_state", 0)

        self.ir_detected_state = int(self.get_parameter("ir_detected_state").value)
        self._last_ir: Optional[bool] = None
        self._last_poll = 0.0

        self.raw_pub = self.create_publisher(String, "/esp32_sensor/raw", 10)
        self.sensor_pub = self.create_publisher(String, "/trash_bin/sensors", 10)
        self.alert_pub = self.create_publisher(String, "/trash_bin/alerts", 10)
        self.levels_pub = self.create_publisher(Int32MultiArray, "/trash_bin/levels", 10)
        self.object_pub = self.create_publisher(Bool, "/trash_bin/object_detected", 10)
        self.create_subscription(String, "/esp32_sensor/cmd", self._on_command, 10)

        self.worker = SerialWorker(
            port=str(self.get_parameter("port").value),
            baudrate=int(self.get_parameter("baudrate").value),
            timeout=float(self.get_parameter("read_timeout").value),
            reconnect_delay=float(self.get_parameter("reconnect_delay").value),
            on_line=self._on_line,
            on_error=lambda msg: self.get_logger().warn(msg),
            on_connect=lambda port: self.get_logger().info(f"connected to sensor ESP32 on {port}"),
        )
        self.worker.start()

        self.poll_timer = self.create_timer(0.2, self._poll_sensor)

    def destroy_node(self) -> bool:
        self.worker.stop()
        return super().destroy_node()

    def _poll_sensor(self) -> None:
        interval = float(self.get_parameter("poll_interval").value)
        if interval <= 0:
            return
        now = time.monotonic()
        if now - self._last_poll >= interval:
            self._last_poll = now
            self.worker.write_line("CMD:READ_SENSORS")

    def _on_command(self, msg: String) -> None:
        command = msg.data.strip()
        if not command:
            return
        if not self.worker.write_line(command):
            self.get_logger().warn(f"sensor ESP32 is not connected; dropped {command}")

    def _on_line(self, line: str) -> None:
        self.raw_pub.publish(String(data=line))
        try:
            if line.startswith("SENSOR:"):
                data = parse_sensor_payload(line.removeprefix("SENSOR:"))
                self.sensor_pub.publish(String(data=json_dumps(data)))
                if "ir_state" in data:
                    self._publish_ir(int(data["ir_state"]))
            elif line.startswith("LEVELS:"):
                levels = parse_levels_payload(line.removeprefix("LEVELS:"))
                self.levels_pub.publish(Int32MultiArray(data=levels))
            elif line.startswith("BATTERY:"):
                data = {"vbat": float(line.removeprefix("BATTERY:").strip())}
                self.sensor_pub.publish(String(data=json_dumps(data)))
            elif line.startswith("ALERT:"):
                self.alert_pub.publish(String(data=line.removeprefix("ALERT:").strip().lower()))
            elif line.startswith("IR:"):
                self._publish_ir(int(line.removeprefix("IR:").strip()))
            else:
                parsed = parse_key_value_line(line)
                if parsed and parsed.get("label") == "IR" and "state" in parsed:
                    self._publish_ir(int(parsed["state"]))
        except Exception as exc:
            self.get_logger().warn(f"cannot parse sensor line {line!r}: {exc}")

    def _publish_ir(self, state: int) -> None:
        detected = state == self.ir_detected_state
        if self._last_ir is detected:
            return
        self._last_ir = detected
        self.object_pub.publish(Bool(data=detected))


def main() -> None:
    rclpy.init()
    node = SensorBridge()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
