# Run Commands

## 1. Mapping USB hien tai

Theo may cua ban:

- ESP32 sensor: `/dev/serial/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.3:1.0-port0` -> `ttyUSB1`
- ESP32 actuator: `/dev/serial/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.4:1.0-port0` -> `ttyUSB0`

Kiem tra lai:

```bash
ls -l /dev/serial/by-path/
```

Khong nen dung rule chung theo `idVendor/idProduct` cho ca 2 con CH340, vi 2 board giong nhau se de bi nham.

## 2. Copy package sang Pi va build

Tren Pi:

```bash
mkdir -p ~/trash_ws/src
cd ~/trash_ws/src
```

Copy folder `robot/Pi/trash_sorting_ros` tu may dev sang:

```bash
scp -r trash_sorting_ros giap@<PI_IP>:~/trash_ws/src/
```

Build:

```bash
cd ~/trash_ws
python3 -m pip install requests opencv-python ultralytics numpy PyYAML pyserial
colcon build --symlink-install
source install/setup.bash
```

Neu vua sua `src/trash_sorting_ros/config/pipeline.yaml` ma launch van doc config cu, chay lai 2 lenh nay:

```bash
cd ~/trash_ws
colcon build --symlink-install
source install/setup.bash
```

Model YOLO da nam san trong package:

```bash
~/trash_ws/src/trash_sorting_ros/models/best_model.pt
```

## 3. Kiem tra thiet bi USB

```bash
lsusb
ls -l /dev/serial/by-path/
ls -l /dev/video*
```

Kiem tra camera Logitech C270:

```bash
v4l2-ctl --list-devices
```

Neu camera khong phai `/dev/video0`, sua `camera_index` trong:

```bash
nano ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Logitech C270 can return dark first frames after opening. The YOLO node skips warm-up frames with:

```yaml
camera_warmup_frames: 20
camera_warmup_delay_seconds: 0.05
```

## 4. Test camera bang Python

```bash
python3 - <<'PY'
import cv2
cap = cv2.VideoCapture(0)
ok, frame = cap.read()
print("ok=", ok, "shape=", None if frame is None else frame.shape)
if ok:
    cv2.imwrite("/tmp/c270_test.jpg", frame)
cap.release()
PY
```

## 5. Chay sensor bridge

Terminal 1:

```bash
cd ~/trash_ws
source install/setup.bash
ros2 run trash_sorting_ros sensor_bridge --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Terminal 2, xem raw:

```bash
source ~/trash_ws/install/setup.bash
ros2 topic echo /esp32_sensor/raw
```

Xem data da parse:

```bash
ros2 topic echo /trash_bin/sensors
ros2 topic echo /trash_bin/levels
ros2 topic echo /trash_bin/object_detected
ros2 topic echo /trash_bin/alerts
```

Force doc sensor:

```bash
ros2 topic pub --once /esp32_sensor/cmd std_msgs/msg/String "{data: 'CMD:READ_SENSORS'}"
ros2 topic pub --once /esp32_sensor/cmd std_msgs/msg/String "{data: 'CMD:READ_LEVELS'}"
```

## 6. Chay Firebase bridge

Neu database public thi khong can `FIREBASE_AUTH_TOKEN`.

```bash
cd ~/trash_ws
source install/setup.bash
ros2 run trash_sorting_ros firebase_bridge --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Kiem tra node:

```bash
ros2 node list
```

Can thay:

```text
/sensor_bridge
/firebase_bridge
```

## 7. Test YOLO rieng

Terminal 1:

```bash
cd ~/trash_ws
source install/setup.bash
ros2 run trash_sorting_ros yolo_classifier --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Terminal 2:

```bash
source ~/trash_ws/install/setup.bash
ros2 topic pub --once /trash_bin/classify_request std_msgs/msg/Empty "{}"
ros2 topic echo /trash_bin/classification
```

Mapping:

- `Metal` -> bin `0`
- `Paper` -> bin `1`
- `Glass`, `Plastic`, `Waste`, no detection -> bin `2`

## 8. Test actuator bridge

Chay bridge:

```bash
cd ~/trash_ws
source install/setup.bash
ros2 run trash_sorting_ros actuator_bridge --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Luu y:

- ESP32 actuator noi voi Pi qua cong Type-C.
- Firmware actuator phai doc/ghi qua `Serial` USB, khong phai `Serial2`.
- Baudrate actuator bridge hien tai la `115200`.

Gui command test:

```bash
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:STATUS'}"
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:CLASSIFY:0'}"
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:CLASSIFY:1'}"
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:CLASSIFY:2'}"
```

Test do line:

```bash
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:MOVE_START'}"
ros2 topic pub --once /esp32_actuator/cmd std_msgs/msg/String "{data: 'CMD:MOVE_STOP'}"
```

Xem status/telemetry:

```bash
ros2 topic echo /esp32_actuator/status
ros2 topic echo /trash_bin/actuator
```

## 9. Chay full pipeline

```bash
cd ~/trash_ws
source install/setup.bash
ros2 launch trash_sorting_ros bringup.launch.py
```

Hoac ep launch dung thang file config trong `src`:

```bash
cd ~/trash_ws
source install/setup.bash
ros2 launch trash_sorting_ros bringup.launch.py config:=/home/giap/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Full flow:

1. ESP32 sensor gui IR.
2. Pi nhan `/trash_bin/object_detected`.
3. Pi bat camera, chay YOLO.
4. Pi gui `CMD:CLASSIFY:<0|1|2>` xuong ESP32 actuator.
5. ESP32 actuator quay SG2 chon o, SG1 quay 45 do cho rac roi.
6. ESP32 sensor cap nhat muc rac.
7. Pi day data len Firebase.

## 9.0. Tu dong chay full pipeline khi bat Pi

Dung `systemd` de Pi boot xong tu vao pixi environment roi chay tat ca node trong `bringup.launch.py`:

- `sensor_bridge`
- `actuator_bridge`
- `yolo_classifier`
- `trash_orchestrator`
- `firebase_bridge`

Tren Pi, dam bao workspace da build duoc bang lenh o muc 2, sau do:

```bash
cd ~/trash_ws
chmod +x ~/trash_ws/src/trash_sorting_ros/scripts/start_full_pipeline.sh
sudo cp ~/trash_ws/src/trash_sorting_ros/systemd/trash-sorting.service /etc/systemd/system/trash-sorting.service
```

Service mac dinh dung cac duong dan:

```text
Workspace: /home/giap/trash_ws
Pixi env:  /home/giap/robot/Pi/ros2_env
Pixi bin:  /home/giap/.pixi/bin/pixi
```

Neu Pi cua ban cai `pixi` o duong dan khac, kiem tra bang:

```bash
which pixi
```

roi sua dong `PIXI_BIN=` trong service.

Neu tren Pi chua co folder `systemd` trong package, copy file service tu may dev sang Pi:

```bash
scp robot/Pi/trash_sorting_ros/systemd/trash-sorting.service giap@<PI_IP>:/tmp/trash-sorting.service
ssh giap@<PI_IP>
sudo cp /tmp/trash-sorting.service /etc/systemd/system/trash-sorting.service
```

Cap quyen serial/camera cho user `giap`:

```bash
sudo usermod -aG dialout,video giap
sudo reboot
```

Sau khi Pi boot lai, bat service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable trash-sorting.service
sudo systemctl start trash-sorting.service
```

Kiem tra trang thai va log:

```bash
systemctl status trash-sorting.service
journalctl -u trash-sorting.service -f
```

Tat autostart khi can debug thu cong:

```bash
sudo systemctl stop trash-sorting.service
sudo systemctl disable trash-sorting.service
```

Neu user tren Pi khong phai `giap`, sua cac dong `User=`, `Group=`, `/home/giap/...` trong:

```bash
sudo nano /etc/systemd/system/trash-sorting.service
sudo systemctl daemon-reload
sudo systemctl restart trash-sorting.service
```

## 9.1. Lenh di do rac tu Firebase/UI

UI nut `Di do rac` ghi:

```text
/bins/bin_001/commands/go_dump = true
```

`firebase_bridge` poll field nay moi 1 giay, publish `/trash_bin/go_dump_request`, roi reset:

```text
/bins/bin_001/commands/go_dump = false
```

Test khong can UI:

```bash
curl -X PATCH \
  -H "Content-Type: application/json" \
  -d '{"commands/go_dump":true}' \
  "https://trash-detection-9d793-default-rtdb.firebaseio.com/bins/bin_001.json"
```

Flow:

1. Pi gui `CMD:MOVE_START`.
2. ESP32 actuator do line den marker cuoi, gui `STATUS:ARRIVED_DUMP`.
3. Pi set state `awaiting_return` va dung tai diem do.

## 9.2. Lenh ve nha tu Firebase/UI

UI nut `Ve nha` ghi:

```text
/bins/bin_001/commands/go_home = true
```

`firebase_bridge` poll field nay moi 1 giay, publish `/trash_bin/go_home_request`, roi reset:

```text
/bins/bin_001/commands/go_home = false
```

Test khong can UI:

```bash
curl -X PATCH \
  -H "Content-Type: application/json" \
  -d '{"commands/go_home":true}' \
  "https://trash-detection-9d793-default-rtdb.firebaseio.com/bins/bin_001.json"
```

Flow:

1. Pi gui `CMD:MOVE_HOME`.
2. ESP32 actuator quay 180 do theo `TURN_AROUND_MS`, tim lai line, do line ve.
3. Den marker cuoi, ESP32 gui `STATUS:ARRIVED_HOME`.
4. Pi set state ve `idle`.

## 10. Topic debug nhanh

```bash
ros2 topic list
ros2 topic echo /esp32_sensor/raw
ros2 topic echo /trash_bin/sensors
ros2 topic echo /trash_bin/classification
ros2 topic echo /esp32_actuator/status
ros2 topic echo /trash_bin/actuator
ros2 topic echo /trash_bin/state
```

## 11. Loi hay gap

Sai params file path:

```bash
ros2 run trash_sorting_ros firebase_bridge --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
```

Thieu `requests`:

```bash
python3 -m pip install requests
```

Khong thay thiet bi serial dung `by-path`:

```bash
lsusb
ls -l /dev/serial/by-path/
```

Khong doc duoc serial: khong chay `cat /dev/ttyUSB0`, `cat /dev/ttyUSB1` hoac `cat /dev/serial/by-path/...` dong thoi voi ROS.

## Chạy không cần IR
ros2 run trash_sorting_ros yolo_classifier --ros-args --params-file ~/trash_ws/src/trash_sorting_ros/config/pipeline.yaml
 ## Terminal khác:
source ~/trash_ws/install/setup.bash
ros2 topic pub --once /trash_bin/classify_request std_msgs/msg/Empty "{}"
