import json
import math
from typing import Any, Dict, List, Optional


SENSOR_FIELDS = [
    "temperature1",
    "humidity1",
    "temperature2",
    "humidity2",
    "temperature3",
    "humidity3",
    "mq2_1",
    "mq2_2",
    "mq2_3",
    "mq135_1",
    "mq135_2",
    "mq135_3",
    "bin1_percent",
    "bin2_percent",
    "bin3_percent",
    "vbat",
]


def _to_float(value: str) -> float:
    value = value.strip()
    if value.lower() in {"nan", "timeout", ""}:
        return math.nan
    return float(value)


def _to_int(value: str) -> int:
    return int(float(value.strip()))


def parse_csv_numbers(payload: str) -> List[float]:
    return [_to_float(part) for part in payload.split(",") if part.strip() != ""]


def parse_sensor_payload(payload: str) -> Dict[str, Any]:
    values = parse_csv_numbers(payload)
    if len(values) < len(SENSOR_FIELDS):
        raise ValueError(f"SENSOR payload needs {len(SENSOR_FIELDS)} values, got {len(values)}")

    data: Dict[str, Any] = {}
    for field, value in zip(SENSOR_FIELDS, values):
        if field.startswith("mq") or field.endswith("_percent"):
            data[field] = int(value)
        else:
            data[field] = None if math.isnan(value) else float(value)

    if len(values) >= len(SENSOR_FIELDS) + 1:
        data["ir_state"] = int(values[len(SENSOR_FIELDS)])

    return data


def parse_levels_payload(payload: str) -> List[int]:
    values = [_to_int(part) for part in payload.split(",") if part.strip() != ""]
    if len(values) != 3:
        raise ValueError(f"LEVELS payload needs 3 values, got {len(values)}")
    return values


def parse_actuator_payload(payload: str) -> Dict[str, Any]:
    parts = [part.strip() for part in payload.split(",")]
    if len(parts) != 15:
        raise ValueError(f"ACT payload needs 15 values, got {len(parts)}")

    raw = [_to_int(value) for value in parts[5:10]]
    strength = [_to_int(value) for value in parts[10:15]]
    return {
        "state": parts[0],
        "moving": bool(_to_int(parts[1])),
        "current_bin": _to_int(parts[2]),
        "line_position": _to_int(parts[3]),
        "line_active_count": _to_int(parts[4]),
        "line_raw": raw,
        "line_strength": strength,
    }


def parse_key_value_line(line: str) -> Optional[Dict[str, Any]]:
    # Supports the current code_test format, for example:
    # IR,state=0
    # DHT1,t=31.2,h=70.1
    if "," not in line:
        return None
    label, *pairs = line.split(",")
    parsed: Dict[str, Any] = {"label": label.strip()}
    for pair in pairs:
        if "=" not in pair:
            continue
        key, value = pair.split("=", 1)
        value = value.strip()
        try:
            parsed[key.strip()] = _to_float(value)
        except ValueError:
            parsed[key.strip()] = value
    return parsed


def json_dumps(data: Dict[str, Any]) -> str:
    return json.dumps(data, separators=(",", ":"), ensure_ascii=True)


def normalize_command(command: str) -> str:
    command = command.strip()
    if not command:
        raise ValueError("empty command")
    return command if command.startswith("CMD:") else f"CMD:{command}"
