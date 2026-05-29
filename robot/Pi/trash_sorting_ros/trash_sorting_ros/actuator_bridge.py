from __future__ import annotations

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

from .protocol import json_dumps, normalize_command, parse_actuator_payload
from .serial_node import SerialWorker


class ActuatorBridge(Node):
    def __init__(self) -> None:
        super().__init__("actuator_bridge")
        self.declare_parameter(
            "port",
            "/dev/serial/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.4:1.0-port0",
        )
        self.declare_parameter("baudrate", 115200)
        self.declare_parameter("read_timeout", 0.2)
        self.declare_parameter("reconnect_delay", 2.0)

        self.raw_pub = self.create_publisher(String, "/esp32_actuator/raw", 10)
        self.status_pub = self.create_publisher(String, "/esp32_actuator/status", 10)
        self.telemetry_pub = self.create_publisher(String, "/trash_bin/actuator", 10)
        self.cmd_sub = self.create_subscription(String, "/esp32_actuator/cmd", self._on_command, 10)

        self.worker = SerialWorker(
            port=str(self.get_parameter("port").value),
            baudrate=int(self.get_parameter("baudrate").value),
            timeout=float(self.get_parameter("read_timeout").value),
            reconnect_delay=float(self.get_parameter("reconnect_delay").value),
            on_line=self._on_line,
            on_error=lambda msg: self.get_logger().warn(msg),
            on_connect=lambda port: self.get_logger().info(f"connected to actuator ESP32 on {port}"),
        )
        self.worker.start()

    def destroy_node(self) -> bool:
        self.worker.stop()
        return super().destroy_node()

    def _on_command(self, msg: String) -> None:
        try:
            command = normalize_command(msg.data)
        except ValueError:
            return
        self.get_logger().warn(f"tx actuator command: {command}")
        self.raw_pub.publish(String(data=f"BRIDGE_TX:{command}"))
        if self.worker.write_line(command):
            self.raw_pub.publish(String(data=f"BRIDGE_TX_OK:{command}"))
        else:
            self.raw_pub.publish(String(data=f"BRIDGE_TX_FAIL:{command}"))
            self.get_logger().warn(f"actuator ESP32 is not connected; dropped {command}")

    def _on_line(self, line: str) -> None:
        self.raw_pub.publish(String(data=line))
        if line.startswith("STATUS:"):
            self.status_pub.publish(String(data=line))
        elif line.startswith("ACT:"):
            try:
                data = parse_actuator_payload(line.removeprefix("ACT:"))
            except Exception as exc:
                self.get_logger().warn(f"cannot parse actuator line {line!r}: {exc}")
                return
            self.telemetry_pub.publish(String(data=json_dumps(data)))


def main() -> None:
    rclpy.init()
    node = ActuatorBridge()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
