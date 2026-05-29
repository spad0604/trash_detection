from __future__ import annotations

import json
import os
import time
from pathlib import Path
from typing import Any, Dict, Optional

import rclpy
from rclpy.node import Node
from std_msgs.msg import Empty, String

try:
    from ament_index_python.packages import get_package_share_directory
except ImportError:  # pragma: no cover - ROS runtime dependency
    get_package_share_directory = None


class YoloClassifier(Node):
    def __init__(self) -> None:
        super().__init__("yolo_classifier")
        self.declare_parameter("model_path", os.getenv("TRASH_YOLO_MODEL", ""))
        self.declare_parameter("camera_index", 0)
        self.declare_parameter("image_width", 640)
        self.declare_parameter("image_height", 480)
        self.declare_parameter("camera_warmup_frames", 20)
        self.declare_parameter("camera_warmup_delay_seconds", 0.05)
        self.declare_parameter("confidence_threshold", 0.25)
        self.declare_parameter("fallback_bin", 2)
        self.declare_parameter("debug_image_dir", "")
        self.declare_parameter(
            "trash_class_map",
            '{"metal":0,"paper":1,"glass":2,"plastic":2,"waste":2,"other":2,"0":2,"1":0,"2":1,"3":2,"4":2}',
        )

        self.publisher = self.create_publisher(String, "/trash_bin/classification", 10)
        self.create_subscription(Empty, "/trash_bin/classify_request", self._on_request, 10)
        self._model = None

    def _on_request(self, _: Empty) -> None:
        result = self._classify()
        self.publisher.publish(String(data=json.dumps(result, separators=(",", ":"), ensure_ascii=True)))

    def _classify(self) -> Dict[str, Any]:
        frame = self._capture_frame()
        model_path = self._resolve_model_path(str(self.get_parameter("model_path").value))
        if frame is None:
            return self._fallback("camera_unavailable")
        if not model_path:
            return self._fallback("model_path_not_configured")

        try:
            result = self._run_yolo(frame, model_path)
        except Exception as exc:
            self.get_logger().warn(f"YOLO inference failed: {exc}")
            return self._fallback("inference_failed")

        if result is None:
            return self._fallback("no_detection")
        return result

    def _capture_frame(self) -> Optional[Any]:
        try:
            import cv2
        except ImportError:
            self.get_logger().warn("opencv-python is not installed")
            return None

        camera_index = int(self.get_parameter("camera_index").value)
        cap = cv2.VideoCapture(camera_index)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, int(self.get_parameter("image_width").value))
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, int(self.get_parameter("image_height").value))
        ok = False
        frame = None
        warmup_frames = max(1, int(self.get_parameter("camera_warmup_frames").value))
        warmup_delay = max(0.0, float(self.get_parameter("camera_warmup_delay_seconds").value))
        for _ in range(warmup_frames):
            ok, frame = cap.read()
            if warmup_delay:
                time.sleep(warmup_delay)
        cap.release()
        if not ok:
            return None

        debug_dir = str(self.get_parameter("debug_image_dir").value)
        if debug_dir:
            self._save_debug_image(frame, "capture")
        return frame

    def _run_yolo(self, frame: Any, model_path: str) -> Optional[Dict[str, Any]]:
        try:
            from ultralytics import YOLO
        except ImportError as exc:
            raise RuntimeError("ultralytics is not installed") from exc

        if self._model is None:
            self._model = YOLO(model_path)

        yolo_result = self._model(frame, verbose=False)[0]
        debug_dir = str(self.get_parameter("debug_image_dir").value)
        if debug_dir:
            self._save_debug_image(yolo_result.plot(), "annotated")

        boxes = getattr(yolo_result, "boxes", None)
        if boxes is None or len(boxes) == 0:
            return None

        best = max(boxes, key=lambda box: float(box.conf[0]))
        confidence = float(best.conf[0])
        if confidence < float(self.get_parameter("confidence_threshold").value):
            return None

        raw_class_id = int(best.cls[0])
        names = getattr(yolo_result, "names", {}) or {}
        detected_label = str(names.get(raw_class_id, raw_class_id)).lower()
        bin_index = self._bin_for_label(detected_label, raw_class_id)
        bin_label = self._bin_label(bin_index)

        return {
            "label": bin_label,
            "detected_label": detected_label,
            "class_id": raw_class_id,
            "bin_index": bin_index,
            "confidence": round(confidence, 4),
            "source": "yolo",
            "debug_image": str(Path(debug_dir) / "latest_annotated.jpg") if debug_dir else "",
        }

    def _bin_for_label(self, label: str, class_id: int) -> int:
        try:
            class_map = json.loads(str(self.get_parameter("trash_class_map").value))
        except json.JSONDecodeError:
            class_map = {}
        if label in class_map:
            return max(0, min(2, int(class_map[label])))
        if str(class_id) in class_map:
            return max(0, min(2, int(class_map[str(class_id)])))
        return max(0, min(2, int(self.get_parameter("fallback_bin").value)))

    @staticmethod
    def _bin_label(bin_index: int) -> str:
        return {0: "metal", 1: "paper", 2: "other"}.get(bin_index, "other")

    def _resolve_model_path(self, model_path: str) -> str:
        env_path = os.getenv("TRASH_YOLO_MODEL", "").strip()
        if env_path:
            return env_path
        if not model_path:
            return ""

        path = Path(model_path).expanduser()
        if path.is_absolute():
            return str(path)

        if get_package_share_directory is not None:
            try:
                share_dir = Path(get_package_share_directory("trash_sorting_ros"))
                package_path = share_dir / path
                if package_path.exists():
                    return str(package_path)
            except Exception as exc:
                self.get_logger().warn(f"cannot resolve package model path: {exc}")

        cwd_source_path = Path.cwd() / "src" / "trash_sorting_ros" / path
        if cwd_source_path.exists():
            return str(cwd_source_path)

        home_source_path = Path.home() / "trash_ws" / "src" / "trash_sorting_ros" / path
        if home_source_path.exists():
            return str(home_source_path)

        cwd_path = Path.cwd() / path
        if cwd_path.exists():
            return str(cwd_path)

        return str(path)

    def _fallback(self, reason: str) -> Dict[str, Any]:
        fallback_bin = max(0, min(2, int(self.get_parameter("fallback_bin").value)))
        self.get_logger().warn(f"using fallback classification: {reason}, bin={fallback_bin}")
        return {
            "label": "other",
            "class_id": fallback_bin,
            "bin_index": fallback_bin,
            "confidence": 0.0,
            "source": reason,
        }

    def _save_debug_image(self, image: Any, prefix: str) -> None:
        try:
            import cv2
        except ImportError:
            return

        debug_dir = Path(str(self.get_parameter("debug_image_dir").value)).expanduser()
        debug_dir.mkdir(parents=True, exist_ok=True)
        latest_path = debug_dir / f"latest_{prefix}.jpg"
        timestamp_path = debug_dir / f"{prefix}_{int(time.time())}.jpg"
        cv2.imwrite(str(latest_path), image)
        cv2.imwrite(str(timestamp_path), image)


def main() -> None:
    rclpy.init()
    node = YoloClassifier()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
