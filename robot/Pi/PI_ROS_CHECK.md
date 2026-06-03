# Pi ROS implementation check

Kiến trúc ROS2 (node/topic/luồng) chi tiết: xem `robot/Pi/trash_sorting_ros/docs/ROS_ARCHITECTURE.md`.

## Files added

- `robot/Pi/trash_sorting_ros/`: ROS2 Python package for Raspberry Pi.
- `sensor_bridge`: reads ESP sensor UART, parses sensors/levels/IR/alerts.
- `actuator_bridge`: writes actuator commands and publishes actuator status.
- `actuator_bridge`: also parses `ACT:` telemetry from ESP32 #2 (khi firmware có gửi).
- `trash_orchestrator`: runs the main flow from IR detection to classification, sorting, LED timing, level update, and full-bin move.
- `yolo_classifier`: camera + YOLO inference node. If no model is configured, it falls back to bin 2 (`other`) so the ROS graph still runs.
- `firebase_bridge`: pushes data to Firebase Realtime Database REST endpoint used by Flutter.

## Hardware split assumed

- ESP sensor board sends values to Pi over USB serial through the Type-C cable.
- ESP actuator/motor board receives commands from Pi over USB serial through the Type-C cable.
- Pi is the master for YOLO, orchestration, and Firebase upload.
- A4988 is not used directly by Pi. Pi only sends `CMD:CLASSIFY:<0|1|2>`; actuator firmware decides how SG1/SG2/SG3 move.

## Sensor pinout from `code_test/esp32_sensor_readout`

| Function | ESP32 pin |
| --- | --- |
| DHT1/DHT2/DHT3 | GPIO5, GPIO18, GPIO19 |
| IR | GPIO4 |
| MQ2_1/MQ2_2/MQ2_3 | GPIO32, GPIO33, GPIO25 |
| MQ135_1/MQ135_2/MQ135_3 | GPIO36 VP, GPIO39 VN, GPIO34 |
| HC-SR04 #1 | TRIG GPIO13, ECHO GPIO14 |
| HC-SR04 #2 | TRIG GPIO27, ECHO GPIO26 |
| HC-SR04 #3 | TRIG GPIO15, ECHO GPIO2 |
| VBAT | GPIO35 |

`robot/MCU/esp32_sensor/esp32_sensor.ino` now uses this pinout and sends sensor data to Pi over USB serial `Serial` at `115200` baud through the Type-C cable. Firebase upload is handled by the Pi ROS `firebase_bridge`, not by the ESP sensor firmware.

## Servo check

The Pi code does not drive ESP32 servo pins directly. It sends high-level commands:

- `CMD:SERVO_OPEN`
- `CMD:SERVO_CLOSE`
- `CMD:CLASSIFY:0`
- `CMD:CLASSIFY:1`
- `CMD:CLASSIFY:2`

From the motor-board schematic, SG1/SG2/SG3 appear to be routed as servo outputs. Please verify the net labels before flashing actuator firmware. If the schematic read is correct, use:

| Servo | Intended role | ESP32 pin |
| --- | --- | --- |
| SG1 | Catch/drop tray | GPIO33 |
| SG2 | Horizontal bin selector | GPIO26 |
| SG3 | Lid/aux mechanism (continuous or positional) | GPIO25 |

Firmware actuator hiện tại đã map đúng các pin trên trong `robot/MCU/esp32_actuator/esp32_actuator.ino`.

## Serial protocol used by Pi

Sensor ESP to Pi:

- `SENSOR:<t1>,<h1>,<t2>,<h2>,<t3>,<h3>,<mq2_1>,<mq2_2>,<mq2_3>,<mq135_1>,<mq135_2>,<mq135_3>,<lvl1>,<lvl2>,<lvl3>,<vbat>,<ir_state>`
- `LEVELS:<l1>,<l2>,<l3>`
- `BATTERY:<voltage>`
- `ALERT:FIRE`
- `ALERT:GAS`
- `IR:<0|1>` is sent immediately when IR state changes.
- The current `code_test` line `IR,state=<0|1>` is also supported for bench testing.

Pi to sensor ESP:

- `CMD:READ_SENSORS`
- `CMD:READ_LEVELS`
- `CMD:READ_BATTERY`
- `CMD:READ_IR`

Pi to actuator ESP:

- `CMD:SERVO_OPEN`
- `CMD:SERVO_CLOSE`
- `CMD:CLASSIFY:<0|1|2>`
- `CMD:MOVE_START`
- `CMD:MOVE_HOME`
- `CMD:MOVE_STOP`
- `CMD:LED:<RED|GREEN|YELLOW|OFF>`

Actuator ESP to Pi:

- `STATUS:RX:<cmd>` echo lại lệnh vừa nhận.
- `STATUS:SERVO_OPENED`
- `STATUS:SERVO_CLOSED`
- `STATUS:SORTING:<0|1|2>`
- `STATUS:SORT_DONE`
- `STATUS:MOVING`
- `STATUS:ARRIVED_DUMP`
- `STATUS:ARRIVED_HOME`
- `STATUS:ARRIVED` (tương thích ngược, nếu firmware cũ dùng 1 trạng thái chung)
- `STATUS:LINE_LOST`
- `STATUS:IDLE`
- `ACT:<state>,<moving>,<bin>,<line_pos>,<active>,<raw1>,<raw2>,<raw3>,<raw4>,<raw5>,<str1>,<str2>,<str3>,<str4>,<str5>` (chỉ xuất hiện khi firmware ESP2 có gửi; trong firmware hiện tại thường gửi khi `CMD:STATUS` hoặc tại các điểm dừng).

## ROS topics

| Topic | Type | Direction |
| --- | --- | --- |
| `/esp32_sensor/raw` | `std_msgs/String` | sensor UART raw lines |
| `/esp32_sensor/cmd` | `std_msgs/String` | commands to sensor ESP |
| `/trash_bin/sensors` | `std_msgs/String` JSON | parsed sensor payload |
| `/trash_bin/levels` | `std_msgs/Int32MultiArray` | bin fill percentages |
| `/trash_bin/object_detected` | `std_msgs/Bool` | IR object event |
| `/trash_bin/alerts` | `std_msgs/String` | `fire`, `gas`, etc. |
| `/trash_bin/classify_request` | `std_msgs/Empty` | request one camera inference |
| `/trash_bin/classification` | `std_msgs/String` JSON | YOLO result and bin index |
| `/esp32_actuator/cmd` | `std_msgs/String` | commands to actuator ESP |
| `/esp32_actuator/status` | `std_msgs/String` | actuator status |
| `/trash_bin/actuator` | `std_msgs/String` JSON | parsed ESP32 #2 telemetry |
| `/trash_bin/state` | `std_msgs/String` | pipeline state for Firebase/UI |

## Runtime flow

1. `sensor_bridge` receives IR detection.
2. `trash_orchestrator` sends `CMD:SERVO_OPEN`.
3. After `lid_open_seconds`, it sends `CMD:SERVO_CLOSE`.
4. It publishes `/trash_bin/classify_request`.
5. `yolo_classifier` captures one frame, runs YOLO, maps the label to bin 0/1/2.
6. `trash_orchestrator` sends `CMD:CLASSIFY:<bin>`.
7. When actuator returns `STATUS:SORT_DONE`, Pi turns LED green for `led_on_seconds` and asks sensor ESP for `CMD:READ_LEVELS`.
8. `firebase_bridge` updates `/bins/bin_001` with sensors, levels, alerts, state, and last classification.
9. If any bin level is `>= full_threshold_percent`, Pi sends `full_bin_move_command` (`CMD:MOVE_START` by default) for line following to the final position.

## Firebase path

The database URL is copied from `user_interface/lib/main.dart`:

`https://trash-detection-9d793-default-rtdb.firebaseio.com`

The bridge writes under:

`/bins/bin_001`

The fields match `TrashBinModel` in Flutter:

- `/sensors/temperature1`, `/sensors/humidity1`, etc.
- `/levels/bin1_percent`, `/levels/bin2_percent`, `/levels/bin3_percent`
- `/battery/voltage`, `/battery/percent`
- `/alerts/fire_risk`, `/alerts/gas_leak`, `/alerts/bin1_full`, etc.
- `/status/state`, `/status/last_classification`, `/status/last_update`
- `/actuator/state`, `/actuator/moving`, `/actuator/current_bin`
- `/navigation/line_position`, `/navigation/line_active_count`, `/navigation/line_raw`, `/navigation/line_strength`

## Build and run on Raspberry Pi

From a ROS2 workspace:

```bash
mkdir -p ~/trash_ws/src
cp -r robot/Pi/trash_sorting_ros ~/trash_ws/src/
cd ~/trash_ws
python3 -m pip install -r src/trash_sorting_ros/requirements.txt
colcon build --symlink-install
source install/setup.bash
ros2 launch trash_sorting_ros bringup.launch.py
```

ESP32 sensor USB is expected at:

```bash
`/dev/serial/by-path/...` (the default in `robot/Pi/trash_sorting_ros/config/pipeline.yaml`).
```

Optional udev rule (if you prefer stable symlinks):

```bash
KERNEL=="ttyUSB*", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666", SYMLINK+="ttyRobot"
```

For USB connection checks, edit `robot/Pi/trash_sorting_ros/config/pipeline.yaml` only if Linux assigns a different device:

- sensor port: `sensor_bridge.ros__parameters.port`
- actuator port: `actuator_bridge.ros__parameters.port`

If Firebase rules require auth, export:

```bash
export FIREBASE_AUTH_TOKEN='<database-auth-token>'
```

The YOLO model is included inside the ROS package:

```bash
robot/Pi/trash_sorting_ros/models/best_model.pt
```

After `colcon build`, `config/pipeline.yaml` resolves:

```bash
models/best_model.pt
```

relative to the installed package share directory.

YOLO classes are mapped to robot bins as:

- `Metal` -> bin 0
- `Paper` -> bin 1
- `Glass`, `Plastic`, `Waste`, no detection -> bin 2
