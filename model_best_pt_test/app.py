from __future__ import annotations

from pathlib import Path
from typing import Any

import cv2
import numpy as np
import streamlit as st
from PIL import Image, ImageDraw, ImageFont
from ultralytics import YOLO


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL_PATH = ROOT / "robot" / "Pi" / "trash_sorting_ros" / "models" / "best_model.pt"

BIN_LABELS = {
    0: "Tái chế",
    1: "Hữu cơ",
    2: "Khác",
}

BIN_COLORS = {
    0: (36, 180, 80),
    1: (52, 152, 219),
    2: (140, 140, 140),
}

CLASS_TO_BIN = {
    "biodegradable": 1,
    "cardboard": 0,
    "glass": 2,
    "metal": 0,
    "paper": 0,
    "plastic": 0,
    "other": 2,
    "0": 1,
    "1": 0,
    "2": 2,
    "3": 0,
    "4": 0,
    "5": 0,
}


@st.cache_resource
def load_model(model_path: str) -> YOLO:
    return YOLO(model_path)


def bin_for_label(raw_label: str, class_id: int) -> int:
    key = raw_label.lower().strip()
    return max(0, min(2, int(CLASS_TO_BIN.get(key, CLASS_TO_BIN.get(str(class_id), 2)))))


def load_font(size: int = 22) -> ImageFont.ImageFont:
    for font_path in (
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        try:
            return ImageFont.truetype(font_path, size)
        except Exception:
            continue
    return ImageFont.load_default()


def draw_label(
    image_rgb: np.ndarray,
    x: int,
    y: int,
    text: str,
    color_rgb: tuple[int, int, int],
) -> np.ndarray:
    pil_image = Image.fromarray(image_rgb)
    draw = ImageDraw.Draw(pil_image)
    font = load_font(20)
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    label_y = max(0, y - text_h - 12)
    draw.rectangle((x, label_y, x + text_w + 12, label_y + text_h + 10), fill=color_rgb)
    draw.text((x + 6, label_y + 5), text, font=font, fill=(255, 255, 255))
    return np.array(pil_image)


def annotate(
    image_rgb: np.ndarray,
    result: Any,
    confidence_threshold: float,
) -> tuple[np.ndarray, list[dict[str, Any]]]:
    annotated = image_rgb.copy()
    rows: list[dict[str, Any]] = []
    boxes = getattr(result, "boxes", None)
    names = getattr(result, "names", {}) or {}

    if boxes is None or len(boxes) == 0:
        return annotated, rows

    for box in boxes:
        confidence = float(box.conf[0])
        if confidence < confidence_threshold:
            continue

        class_id = int(box.cls[0])
        raw_label = str(names.get(class_id, class_id))
        bin_index = bin_for_label(raw_label, class_id)
        group_label = BIN_LABELS.get(bin_index, "Khác")
        color_rgb = BIN_COLORS.get(bin_index, BIN_COLORS[2])

        x1, y1, x2, y2 = [int(round(value)) for value in box.xyxy[0].tolist()]
        cv2.rectangle(annotated, (x1, y1), (x2, y2), color_rgb, 3)
        annotated = draw_label(annotated, x1, y1, f"{group_label} {confidence:.2f}", color_rgb)

        rows.append(
            {
                "raw_class": raw_label,
                "group": group_label,
                "bin_index": bin_index,
                "confidence": round(confidence, 4),
                "box": [x1, y1, x2, y2],
            }
        )

    rows.sort(key=lambda item: item["confidence"], reverse=True)
    return annotated, rows


def main() -> None:
    st.set_page_config(page_title="Test best_model.pt", layout="wide")
    st.title("Test model best_model.pt")

    model_path = st.sidebar.text_input("Model path", str(DEFAULT_MODEL_PATH))
    confidence_threshold = st.sidebar.slider("Confidence threshold", 0.05, 0.95, 0.25, 0.05)

    model_file = Path(model_path)
    if not model_file.exists():
        st.error(f"Không thấy model: {model_file}")
        return

    model = load_model(str(model_file))
    st.sidebar.write("Classes:")
    st.sidebar.json(model.names)

    uploaded_file = st.file_uploader("Upload ảnh để detect", type=["jpg", "jpeg", "png", "bmp", "webp"])
    if uploaded_file is None:
        st.info("Chọn một ảnh để bắt đầu.")
        return

    image = Image.open(uploaded_file).convert("RGB")
    image_rgb = np.array(image)

    with st.spinner("Đang detect..."):
        result = model(image_rgb, verbose=False)[0]
        annotated, rows = annotate(image_rgb, result, confidence_threshold)

    left, right = st.columns(2)
    with left:
        st.subheader("Ảnh gốc")
        st.image(image_rgb, use_container_width=True)
    with right:
        st.subheader("Kết quả detect")
        st.image(annotated, use_container_width=True)

    if rows:
        st.subheader("Danh sách detection")
        st.dataframe(rows, use_container_width=True)
        best = rows[0]
        st.success(
            f"Kết quả mạnh nhất: {best['raw_class']} -> {best['group']} "
            f"(bin {best['bin_index']}), confidence {best['confidence']}"
        )
    else:
        st.warning("Không có detection nào vượt ngưỡng confidence.")


if __name__ == "__main__":
    main()
