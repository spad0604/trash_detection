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
            '{"biodegradable":1,"cardboard":0,"glass":2,"metal":0,"paper":0,"plastic":0,"other":2,"0":1,"1":0,"2":2,"3":0,"4":0,"5":0}',
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

        boxes = getattr(yolo_result, "boxes", None)
        if boxes is None or len(boxes) == 0:
            return None

        names = getattr(yolo_result, "names", {}) or {}
        if debug_dir:
            self._save_debug_image(self._draw_grouped_boxes(frame, boxes, names), "annotated")

        best = max(boxes, key=lambda box: float(box.conf[0]))
        confidence = float(best.conf[0])
        if confidence < float(self.get_parameter("confidence_threshold").value):
            return None

        raw_class_id = int(best.cls[0])
        detected_label = str(names.get(raw_class_id, raw_class_id)).lower()
        bin_index = self._bin_for_label(detected_label, raw_class_id)
        group_label = self._bin_label(bin_index)
        display_label = self._display_label(bin_index)

        return {
            "label": group_label,
            "display_label": display_label,
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
        return {0: "Tái chế", 1: "Hữu cơ", 2: "Khác"}.get(bin_index, "Khác")

    @staticmethod
    def _display_label(bin_index: int) -> str:
        # OpenCV's built-in font is ASCII-oriented, so debug images use
        # unaccented text while JSON/Firebase keeps Vietnamese labels.
        return {0: "Tai che", 1: "Huu co", 2: "Khac"}.get(bin_index, "Khac")

    def _draw_grouped_boxes(self, frame: Any, boxes: Any, names: Dict[int, Any]) -> Any:
        try:
            import cv2
        except ImportError:
            return frame

        annotated = frame.copy()
        for box in boxes:
            confidence = float(box.conf[0])
            if confidence < float(self.get_parameter("confidence_threshold").value):
                continue

            class_id = int(box.cls[0])
            detected_label = str(names.get(class_id, class_id)).lower()
            bin_index = self._bin_for_label(detected_label, class_id)
            label = f"{self._bin_label(bin_index)} {confidence:.2f}"

            xyxy = box.xyxy[0].tolist()
            x1, y1, x2, y2 = [int(round(value)) for value in xyxy]
            color = {
                0: (36, 180, 80),
                1: (52, 152, 219),
                2: (140, 140, 140),
            }.get(bin_index, (140, 140, 140))

            cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 2)
            annotated = self._draw_box_label(annotated, x1, y1, label, color)

        return annotated

    def _draw_box_label(self, image: Any, x: int, y: int, label: str, color: tuple[int, int, int]) -> Any:
        try:
            import cv2
            from PIL import Image, ImageDraw, ImageFont
        except ImportError:
            return self._draw_ascii_box_label(image, x, y, label, color)

        try:
            rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            pil_image = Image.fromarray(rgb)
            draw = ImageDraw.Draw(pil_image)
            font = self._load_label_font(18)
            bbox = draw.textbbox((0, 0), label, font=font)
            text_w = bbox[2] - bbox[0]
            text_h = bbox[3] - bbox[1]
            label_y = max(0, y - text_h - 10)
            rgb_color = (color[2], color[1], color[0])
            draw.rectangle((x, label_y, x + text_w + 10, label_y + text_h + 8), fill=rgb_color)
            draw.text((x + 5, label_y + 4), label, font=font, fill=(255, 255, 255))
            return cv2.cvtColor(
                # numpy is already imported by cv2 in the runtime path; import lazily to keep startup light.
                __import__("numpy").array(pil_image),
                cv2.COLOR_RGB2BGR,
            )
        except Exception:
            return self._draw_ascii_box_label(image, x, y, label, color)

    @staticmethod
    def _load_label_font(size: int) -> Any:
        from PIL import ImageFont

        for font_path in (
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "C:/Windows/Fonts/arial.ttf",
        ):
            try:
                return ImageFont.truetype(font_path, size)
            except Exception:
                continue
        return ImageFont.load_default()

    def _draw_ascii_box_label(self, image: Any, x: int, y: int, label: str, color: tuple[int, int, int]) -> Any:
        try:
            import cv2
        except ImportError:
            return image

        ascii_label = (
            label.replace("Tái chế", "Tai che")
            .replace("Hữu cơ", "Huu co")
            .replace("Khác", "Khac")
        )
        text_size, baseline = cv2.getTextSize(ascii_label, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 2)
        text_w, text_h = text_size
        label_y1 = max(0, y - text_h - baseline - 6)
        label_y2 = max(text_h + baseline + 6, y)
        cv2.rectangle(image, (x, label_y1), (x + text_w + 8, label_y2), color, -1)
        cv2.putText(
            image,
            ascii_label,
            (x + 4, label_y2 - baseline - 3),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (255, 255, 255),
            2,
            cv2.LINE_AA,
        )
        return image

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
            "label": self._bin_label(fallback_bin),
            "display_label": self._display_label(fallback_bin),
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
