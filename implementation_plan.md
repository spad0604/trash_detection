# Kế hoạch triển khai: Thùng rác thông minh phân loại tự động ứng dụng Edge AI và IoT Cloud

Dự án Thùng rác thông minh tích hợp nhận diện hình ảnh qua Edge AI (Raspberry Pi 4B), giám sát môi trường, phân loại tự động và đồng bộ dữ liệu lên IoT Cloud (Firebase). Kế hoạch này mô tả chi tiết các module, luồng hoạt động, cấu hình phần cứng/chân GPIO cho Pi và 2 vi điều khiển ESP32, cùng kiến trúc phần mềm sử dụng Arduino IDE.

## 1. Kiến trúc Hệ thống
Hệ thống được chia làm 3 node xử lý chính hoạt động song song để đảm bảo hiệu suất:
*   **Raspberry Pi 4B 4GB RAM (Edge AI & Master Controller)**: 
    *   Xử lý nhận diện vật thể (YOLO/MobileNet + ROS).
    *   Quản lý Camera USB.
    *   Đọc dữ liệu định vị từ module GPS.
    *   Giao tiếp mạng 4G/LTE qua module SIM A7680C.
    *   Giao tiếp UART với 2 board ESP32.
*   **ESP32 #1 (Sensors & Power Management)**: Chuyên trách đọc các cảm biến môi trường (DHT22, MQ-2, MQ-135), cảm biến siêu âm (HC-SR04) đo mức rác, và đo điện áp bình ắc quy 12V.
*   **ESP32 #2 (Actuators & Navigation)**: Chuyên trách điều khiển cơ cấu chấp hành:
    *   Động cơ Servo đóng/mở nắp.
    *   Mạch A4988 điều khiển động cơ bước (Stepper Motor) xoay đĩa phân phối rác.
    *   Mạch L298N điều khiển động cơ DC (2 bánh xe) để di chuyển.
    *   Đọc cảm biến dò line và bật tắt đèn LED cảnh báo.

## 2. Các Bước Chức năng Chi tiết

### Bước 1: Quản lý Năng lượng & Giám sát Môi trường (Chế độ chờ)
*   **Hoạt động**: ESP32 #1 liên tục đọc dữ liệu từ các cảm biến MQ-2 (khói/gas), MQ-135 (chất lượng không khí), DHT22 (Nhiệt độ, độ ẩm). 
*   **Kiểm tra an toàn**: Nếu phát hiện nhiệt độ quá cao hoặc có khói, gửi tín hiệu ngắt an toàn, truyền dữ liệu cho Pi/Firebase và đẩy cảnh báo Push Notification.

### Bước 2: Tương tác & Tiếp nhận Rác
*   **Hoạt động**: Khi có người tiến lại gần, cảm biến hồng ngoại (hoặc siêu âm) nhận diện. ESP32 #2 điều khiển Servo mở nắp khoang nhận rác. Người dùng bỏ rác vào khay chờ (trên mâm đĩa xoay).

### Bước 3: Phân tích & Nhận diện bằng AI
*   **Hoạt động**: Nắp đóng lại -> Đèn LED trợ sáng bật -> Camera USB trên Raspberry Pi 4B chụp ảnh.
*   **Edge AI**: Pi sử dụng ROS + model YOLO đã train để phân loại rác (Nhựa/Lon, Hữu cơ, Khác).
*   **Giao tiếp**: Trong lúc Pi tính toán, đèn LED đỏ sáng. Kết quả được Pi truyền xuống ESP32 #2 qua UART.

### Bước 4: Điều hướng & Phân loại Cơ khí
*   **Hoạt động**: Nhận được tín hiệu phân loại từ Pi, ESP32 #2 điều khiển module A4988 cấp xung cho Stepper Motor xoay mâm đĩa đến chính xác góc của thùng chứa tương ứng.
*   **Rơi rác**: Rác rớt xuống ngăn. ESP32 #2 điều khiển mâm quay trở về vị trí ban đầu (home).

### Bước 5: Cập nhật Dung lượng
*   **Hoạt động**: Ngay khi rác rơi xuống, ESP32 #1 dùng 3 cảm biến siêu âm (HC-SR04) quét mức rác để tính phần trăm (%) đầy của 3 ngăn. Dữ liệu được đẩy lên Firebase.

### Bước 6: Đồng bộ Cloud & Ứng dụng IoT (Flutter + GetX)
*   **Cơ sở dữ liệu**: Firebase Realtime Database (`trash-detection-9d793-default-rtdb`).
*   **Đẩy dữ liệu**: Raspberry Pi (qua SIM A7680C) hoặc các ESP32 (qua WiFi) cập nhật liên tục:
    *   Mức pin (VBAT).
    *   Dung lượng 3 ngăn rác (%).
    *   Nhiệt độ, độ ẩm, nồng độ khí gas.
*   **Mobile App**: App Flutter quản lý bằng GetX giúp theo dõi bản đồ vị trí thùng rác, các thông số môi trường và nhận Push Notification (cháy nổ/rác đầy).

### Bước 7: Di chuyển tự động (Tập kết rác)
*   **Hoạt động**: Khi 1 trong các ngăn đạt 100%, hệ thống ngắt chế độ nhận rác.
*   **Điều hướng cơ khí**: ESP32 #2 dùng cảm biến dò line và Module L298N điều khiển 2 động cơ bánh xe, di chuyển thùng rác dọc theo line đến khu tập kết.

---

## 3. Bản đồ Chân GPIO Đề xuất

Để hệ thống hoạt động ổn định và tối ưu, dưới đây là quy hoạch GPIO chi tiết cho Raspberry Pi 4B và 2 board ESP32.

### 3.1. Raspberry Pi 4B (Master & AI)
Pi 4B hỗ trợ nhiều cổng UART phần cứng. Kích hoạt thông qua `raspi-config` và `config.txt`.
| Module | Chuẩn giao tiếp | Chân GPIO Raspberry Pi 4B | Ghi chú |
| :--- | :--- | :--- | :--- |
| **Camera** | USB | Cổng USB 3.0 | Tốc độ cao |
| **Module SIM A7680C** | UART0 | `GPIO14` (TXD), `GPIO15` (RXD) | Tích hợp cấp internet |
| **Module GPS** | UART2 | `GPIO0` (TXD2), `GPIO1` (RXD2) | Lấy tọa độ bản đồ |
| **Giao tiếp ESP32 #1** | UART3 | `GPIO4` (TXD3), `GPIO5` (RXD3) | Đọc sensor/pin từ ESP1 |
| **Giao tiếp ESP32 #2** | UART4 | `GPIO8` (TXD4), `GPIO9` (RXD4) | Ra lệnh motor/phân loại |

### 3.2. ESP32 #1 (Sensors Node)
| Linh Kiện | Chân ESP32 | Ghi chú / Cấu hình |
| :--- | :--- | :--- |
| **3x Siêu âm (HC-SR04)** | Trig/Echo: `D12/D13`, `D26/D27`, `D2/D15` | Đo dung lượng rác 3 ngăn |
| **3x Nhiệt ẩm (DHT22)** | `D4`, `D14`, `D18` | Đọc Digital |
| **3x Khí/Khói (MQ-2)** | `D32`, `D33`, `D25` | Đọc Analog (ADC) |
| **3x Chất lượng KK (MQ-135)**| `VP` (36), `VN` (39), `D35` | Đọc Analog (ADC) |
| **Đo điện áp bình (VBAT)**| `D34` | Đọc Analog (qua cầu phân áp) |
| **UART nối với Pi 4B** | `RX2` (16), `TX2` (17) | Cấu hình `Serial2` trên Arduino IDE |

### 3.3. ESP32 #2 (Actuators Node)
| Linh Kiện | Chân ESP32 | Ghi chú / Cấu hình |
| :--- | :--- | :--- |
| **Mạch A4988 (Mâm xoay)**| `D22` (STEP), `D19` (DIR), `D23` (EN) | Thư viện `AccelStepper` |
| **Mạch L298N (Di chuyển)**| `D12` (IN1), `D14` (IN2), `D26` (IN3), `D27` (IN4)<br>`D13` (ENA), `D15` (ENB) | Phát PWM cho ENA, ENB điều tốc |
| **3x Servo** | `D5`, `D18`, `D21` | Phát PWM mở nắp/cơ cấu phụ |
| **Cảm biến Dò Line** | `D4` (Left), `D2` (Right), `D34` (Center) | Đọc Digital/Analog tùy loại sensor |
| **3x LED Cảnh báo** | `D25`, `D32`, `D33` | Báo trạng thái (Xanh/Đỏ/Vàng) |
| **UART nối với Pi 4B** | `RX2` (16), `TX2` (17) | Cấu hình `Serial2` trên Arduino IDE |

---

## 4. Công nghệ & Stack Triển khai

*   **IoT Dashboard & Mobile App**:
    *   **Framework**: Flutter (Đa nền tảng Android/iOS).
    *   **State Management**: GetX (Quản lý trạng thái, Router, Dependency Injection).
    *   **Backend/Cloud**: Firebase Realtime Database (Lưu data realtime), Firebase Cloud Messaging (Push Notifications), Google Maps API (Hiển thị vị trí).
*   **Edge Computing (Raspberry Pi 4B)**:
    *   **OS**: Ubuntu Server hoặc Raspbian.
    *   **Framework**: ROS (Robot Operating System) để quản lý luồng dữ liệu.
    *   **AI Model**: YOLO / MobileNet tích hợp qua Python (OpenCV/TensorRT).
*   **Firmware (ESP32 #1 & #2)**:
    *   **IDE**: Arduino IDE.
    *   **Thư viện**: `Firebase_ESP_Client`, `AccelStepper`, `ESP32Servo`, `DHT sensor library`.
    *   **Cơ chế**: Sử dụng Arduino Core cho ESP32. Khuyến nghị áp dụng `FreeRTOS` (xTaskCreate) để chạy đa luồng, đảm bảo ESP32 #2 vừa điều khiển motor mượt mà vừa nhận lệnh UART liên tục, ESP32 #1 không bị treo khi chờ đọc DHT22.

---

> [!IMPORTANT]
> ## Xác nhận và Các bước tiếp theo
> Bản kế hoạch đã được cập nhật với thiết kế **2 board ESP32 + 1 Raspberry Pi 4B**, IDE lập trình là **Arduino IDE**, và **quy hoạch chân GPIO chuẩn xác** để tận dụng tối đa năng lực phần cứng.
> 
> Nếu bạn đồng ý với kế hoạch này, vui lòng báo lại để chúng ta tiến hành **bước đầu tiên là khởi tạo dự án Flutter** hoặc **viết mã nguồn Firmware cho 2 board ESP32**!
