from __future__ import annotations

import json
from typing import Callable, List

import rclpy
from rclpy.node import Node
from rclpy.timer import Timer
from std_msgs.msg import Bool, Empty, Int32MultiArray, String


class TrashOrchestrator(Node):
    def __init__(self) -> None:
        super().__init__("trash_orchestrator")
        self.declare_parameter("capture_delay_seconds", 0.8)
        self.declare_parameter("capture_timeout_seconds", 5.0)
        self.declare_parameter("sort_timeout_seconds", 12.0)
        self.declare_parameter("led_on_seconds", 3.0)
        self.declare_parameter("full_threshold_percent", 90)
        self.declare_parameter("full_bin_move_command", "CMD:MOVE_START")
        self.declare_parameter(
            "class_to_bin",
            '{"biodegradable":1,"cardboard":0,"glass":2,"metal":0,"paper":0,"plastic":0,"other":2,"0":1,"1":0,"2":2,"3":0,"4":0,"5":0}',
        )

        self.cmd_pub = self.create_publisher(String, "/esp32_actuator/cmd", 10)
        self.sensor_cmd_pub = self.create_publisher(String, "/esp32_sensor/cmd", 10)
        self.classify_request_pub = self.create_publisher(Empty, "/trash_bin/classify_request", 10)
        self.state_pub = self.create_publisher(String, "/trash_bin/state", 10)

        self.create_subscription(Bool, "/trash_bin/object_detected", self._on_object, 10)
        self.create_subscription(Empty, "/trash_bin/go_dump_request", self._on_go_dump_request, 10)
        self.create_subscription(Empty, "/trash_bin/go_home_request", self._on_go_home_request, 10)
        self.create_subscription(Empty, "/trash_bin/stop_request", self._on_stop_request, 10)
        self.create_subscription(String, "/trash_bin/capture_status", self._on_capture_status, 10)
        self.create_subscription(String, "/trash_bin/classification", self._on_classification, 10)
        self.create_subscription(String, "/esp32_actuator/status", self._on_status, 10)
        self.create_subscription(Int32MultiArray, "/trash_bin/levels", self._on_levels, 10)

        self._busy = False
        self._lid_open = False
        self._capture_requested = False
        self._moving_due_to_full = False
        self._dump_trip_active = False
        self._state = "idle"
        self._location = "home"
        self._timers: List[Timer] = []
        self._publish_state("idle")

    def _schedule(self, delay: float, callback: Callable[[], None]) -> None:
        holder = {"timer": None}

        def _wrapped() -> None:
            timer = holder["timer"]
            if timer is not None:
                timer.cancel()
                if timer in self._timers:
                    self._timers.remove(timer)
            callback()

        timer = self.create_timer(max(delay, 0.01), _wrapped)
        holder["timer"] = timer
        self._timers.append(timer)

    def _publish_state(self, state: str) -> None:
        self._state = state
        self.state_pub.publish(String(data=state))

    def _cmd(self, command: str) -> None:
        self.get_logger().info(f"forward actuator command: {command}")
        self.cmd_pub.publish(String(data=command))

    def _sensor_cmd(self, command: str) -> None:
        self.sensor_cmd_pub.publish(String(data=command))

    def _on_object(self, msg: Bool) -> None:
        if not msg.data:
            return
        if self._lid_open or self._state in {"intake_open", "capturing", "sorting"}:
            self.get_logger().warn(
                "ignored object detected while "
                f"state={self._state}, lid_open={self._lid_open}, busy={self._busy}"
            )
            return

        if self._moving_due_to_full or self._dump_trip_active:
            self.get_logger().warn(f"object detected while state={self._state}; stopping movement for intake")
            self._cmd("CMD:MOVE_STOP")

        self._busy = True
        self._moving_due_to_full = False
        self._dump_trip_active = False
        self._lid_open = True
        self._capture_requested = False
        self.get_logger().info("object detected; starting intake flow")
        self._publish_state("intake_open")
        self._cmd("CMD:LED:YELLOW")
        self._cmd("CMD:SERVO_OPEN")
        self._schedule(float(self.get_parameter("capture_delay_seconds").value), self._request_classification)

    def _on_go_dump_request(self, _: Empty) -> None:
        if self._busy or self._dump_trip_active:
            return
        if self._location == "dump":
            self.get_logger().warn("ignored go-dump request while already at dump")
            return
        self.get_logger().info("go-dump request received")
        self._busy = False
        self._location = "moving"
        self._moving_due_to_full = True
        self._dump_trip_active = True
        self._publish_state("dump_outbound")
        self._cmd("CMD:MOVE_START")

    def _on_go_home_request(self, _: Empty) -> None:
        if self._busy:
            return
        if self._state not in {"dump_completed", "arrived", "line_lost", "home_completed"}:
            self.get_logger().warn(f"ignored go-home request while state={self._state}")
            return
        self.get_logger().info("go-home request received")
        self._location = "moving"
        self._moving_due_to_full = True
        self._dump_trip_active = True
        self._publish_state("dump_returning")
        self._cmd("CMD:MOVE_HOME")

    def _on_stop_request(self, _: Empty) -> None:
        self.get_logger().info("stop request received")
        self._busy = False
        self._lid_open = False
        self._capture_requested = False
        self._moving_due_to_full = False
        self._dump_trip_active = False
        for timer in list(self._timers):
            timer.cancel()
        self._timers.clear()
        self._cmd("CMD:MOVE_STOP")
        self._cmd("CMD:LED:OFF")
        self._publish_state("idle")

    def _request_classification(self) -> None:
        if not self._busy or not self._lid_open or self._capture_requested:
            return
        self._capture_requested = True
        self._publish_state("capturing")
        self._cmd("CMD:LED:RED")
        self.classify_request_pub.publish(Empty())
        self._schedule(float(self.get_parameter("capture_timeout_seconds").value), self._capture_timeout)

    def _capture_timeout(self) -> None:
        if self._state != "capturing":
            return
        self.get_logger().warn("capture timeout; closing lid and waiting for classification result")
        self._close_lid()

    def _close_lid(self) -> None:
        if not self._lid_open:
            return
        self._lid_open = False
        self._cmd("CMD:SERVO_CLOSE")

    def _on_capture_status(self, msg: String) -> None:
        if not self._busy or not self._capture_requested:
            return
        status = msg.data.strip().lower()
        if status in {"captured", "capture_failed"}:
            self.get_logger().info(f"camera status={status}; closing lid")
            self._close_lid()

    def _on_classification(self, msg: String) -> None:
        if not self._busy:
            return
        self._close_lid()
        try:
            result = json.loads(msg.data)
        except json.JSONDecodeError:
            result = {"label": msg.data}

        bin_index = self._bin_from_result(result)
        label = str(result.get("label", "unknown"))
        confidence = result.get("confidence")
        if confidence is None:
            self.get_logger().info(f"classification={label}, bin={bin_index}")
        else:
            self.get_logger().info(f"classification={label}, bin={bin_index}, confidence={confidence}")

        self._publish_state("sorting")
        self._cmd(f"CMD:CLASSIFY:{bin_index}")
        self._schedule(float(self.get_parameter("sort_timeout_seconds").value), self._sort_timeout)

    def _bin_from_result(self, result: dict) -> int:
        if "bin_index" in result:
            return max(0, min(2, int(result["bin_index"])))
        if "class_id" in result:
            return max(0, min(2, int(result["class_id"])))

        try:
            class_to_bin = json.loads(str(self.get_parameter("class_to_bin").value))
        except json.JSONDecodeError:
            class_to_bin = {}
        label = str(result.get("label", "other")).lower()
        return max(0, min(2, int(class_to_bin.get(label, class_to_bin.get("other", 2)))))

    def _on_status(self, msg: String) -> None:
        status = msg.data.strip()
        if status == "STATUS:SORT_DONE":
            self._on_sort_done()
        elif status == "STATUS:ARRIVED_DUMP":
            self._location = "dump"
            if self._dump_trip_active or self._moving_due_to_full:
                self._publish_state("dump_completed")
            else:
                self._publish_state("arrived")
        elif status == "STATUS:ARRIVED_HOME":
            self._moving_due_to_full = False
            self._dump_trip_active = False
            self._location = "home"
            self._publish_state("home_completed")
        elif status == "STATUS:ARRIVED":
            self._moving_due_to_full = False
            self._dump_trip_active = False
            self._location = "home"
            self._publish_state("arrived")
        elif status == "STATUS:LINE_LOST":
            self._moving_due_to_full = False
            self._dump_trip_active = False
            self._publish_state("line_lost")

    def _on_sort_done(self) -> None:
        if self._state != "sorting":
            return
        self._capture_requested = False
        self._publish_state("updating_levels")
        self._cmd("CMD:LED:GREEN")
        self._sensor_cmd("CMD:READ_LEVELS")
        self._schedule(float(self.get_parameter("led_on_seconds").value), lambda: self._cmd("CMD:LED:OFF"))
        self._schedule(1.0, self._finish_cycle_if_not_full)

    def _sort_timeout(self) -> None:
        if self._state != "sorting":
            return
        self.get_logger().warn("sort timeout; requesting levels and releasing cycle")
        self._on_sort_done()

    def _finish_cycle_if_not_full(self) -> None:
        if self._state == "updating_levels":
            self._busy = False
            self._capture_requested = False
            self._publish_state("idle")

    def _on_levels(self, msg: Int32MultiArray) -> None:
        if len(msg.data) < 3:
            return
        full_threshold = int(self.get_parameter("full_threshold_percent").value)
        can_start_dump = self._state in {"idle", "updating_levels", "home_completed", "arrived", "line_lost"}
        if (
            max(msg.data[:3]) >= full_threshold
            and can_start_dump
            and self._location != "dump"
            and not self._moving_due_to_full
        ):
            self.get_logger().info(f"bin level reached threshold {full_threshold}%: {list(msg.data[:3])}")
            self._busy = False
            self._location = "moving"
            self._moving_due_to_full = True
            self._dump_trip_active = True
            self._publish_state("bin_full")
            self._cmd(str(self.get_parameter("full_bin_move_command").value))


def main() -> None:
    rclpy.init()
    node = TrashOrchestrator()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
