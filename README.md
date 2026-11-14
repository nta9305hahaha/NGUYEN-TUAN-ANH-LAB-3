# 🌿 Smart Garden – Giám sát môi trường vườn cây công nghiệp (ESP32 • FreeRTOS • MQTT)

Bài tập môn **Hệ thống nhúng** – Đề 2: thiết kế mạch giám sát nhiệt độ, độ ẩm cho vườn cây công nghiệp (cà phê, hồ tiêu) bằng **ESP32 + DHT22**, chạy **FreeRTOS** hai lõi, gửi dữ liệu lên **MQTT Broker** và hiển thị / điều khiển trên **Web Dashboard**.

> ▶️ **Mô phỏng Wokwi:** https://wokwi.com/projects/480202705742088193

## ✨ Tính năng

- Đọc nhiệt độ, độ ẩm từ **DHT22**, in ra **Serial Monitor** (115200 baud).
- Gửi dữ liệu lên MQTT Broker qua WiFi; web hiển thị **2 thẻ số** và **2 biểu đồ thời gian thực** (Chart.js).
- Hai chế độ **AUTO / MANUAL**; ở MANUAL điều khiển **Máy bơm, Quạt, Máy phun sương** bằng nút bấm.
- Giữ độ ẩm vườn trong khoảng **40 % – 60 %**: máy phun sương tự bật khi độ ẩm xuống thấp.
- Kiến trúc **RTOS**: 3 task, 2 queue, 1 mutex, chạy trên cả 2 lõi.

## 🧠 Kiến trúc RTOS

| Task | Core | Chức năng |
|------|:----:|-----------|
| `taskTransmitData` | **0** | Kết nối WiFi → MQTT; nếu thành công, đọc `dataQueue` và publish; nhận lệnh điều khiển từ web |
| `taskReadData` | **1** | Đọc DHT22 (2,5 s/lần), in Serial, gửi dữ liệu đầy đủ vào `dataQueue` và dữ liệu cảnh báo vào `dataAlert` |
| `taskAlert` | **1** | Đọc `dataAlert`, điều khiển bơm / quạt / máy phun sương (chế độ AUTO) |

- `dataQueue`, `dataAlert`: hàng đợi độ dài 1, ghi bằng `xQueueOverwrite()` → luôn giữ **giá trị mới nhất**.
- `controlMutex`: bảo vệ trạng thái điều khiển dùng chung giữa Core 0 (callback MQTT) và Core 1 (`taskAlert`).

```
 CORE 1                                         CORE 0
 taskReadData ──► dataQueue ──────────────────► taskTransmitData ──► MQTT Broker ◄── Web
      │                                                ▲ (lệnh manual qua callback)
      └──────► dataAlert ──► taskAlert ──► Bơm / Quạt / Phun sương
```

## 🔌 Sơ đồ chân

| Thiết bị | Chân ESP32 | Ghi chú |
|----------|:----------:|---------|
| DHT22 (SDA) | GPIO15 | VCC → 3V3, GND → GND |
| Máy bơm (LED + R 220 Ω) | GPIO18 | ON/OFF |
| Quạt (LED + R 220 Ω) | GPIO19 | ON/OFF |
| Máy phun sương (LED + R 1 kΩ) | GPIO21 | PWM 5 kHz, 8 bit |

Sơ đồ đầy đủ nằm trong `diagram.json` (mở bằng Wokwi).

## ⚙️ Thuật toán điều khiển (chế độ AUTO)

**Máy phun sương** (theo đề bài):

| Độ ẩm | Công suất | Thời gian |
|-------|:---------:|:---------:|
| 35 % – 40 % | 25 % | 10 s |
| 30 % – 35 % | 50 % | 20 s |
| < 30 % | 100 % | 30 s |

**Giữ ổn định môi trường** (có trễ – hysteresis, tránh đóng ngắt liên tục):

- Máy bơm: **bật** khi độ ẩm < 40 %, **tắt** khi độ ẩm ≥ 60 %.
- Quạt: **bật** khi nhiệt độ > 35 °C, **tắt** khi nhiệt độ < 30 °C.

## 📡 MQTT

- ESP32: `broker.emqx.io:1883` (TCP). Web: `wss://broker.emqx.io:8084/mqtt` (WebSocket).
- Tiền tố topic: `lab3/smartgarden/esp32_2026_new01` (**đổi thành chuỗi riêng của bạn** vì broker là công cộng; nhớ đổi đồng thời trong `sketch.ino` và `script.js`).

| Topic (sau tiền tố) | Hướng | Nội dung |
|---------------------|:-----:|----------|
| `temperature`, `humidity` | ESP32 → Web | Số thực (°C / %) |
| `mode/set` | Web → ESP32 | `AUTO` / `MANUAL` |
| `pump/set`, `fan/set`, `mist/set` | Web → ESP32 | `ON` / `OFF` (chỉ nhận ở MANUAL) |
| `mode/state`, `pump/state`, `fan/state`, `mist/state` | ESP32 → Web | Trạng thái (retain) |
| `mist/power` | ESP32 → Web | 0 – 100 (% công suất, retain) |

## 📁 Cấu trúc thư mục

```
.
├── sketch.ino      # Chương trình ESP32 (FreeRTOS + MQTT)
├── diagram.json    # Sơ đồ mạch Wokwi
├── libraries.txt   # Thư viện cho Wokwi
├── index.html      # Giao diện web
├── style.css
├── script.js       # MQTT.js + Chart.js
└── docs/           # Báo cáo, ảnh chụp
```

## 🚀 Cách chạy

### 1. Mô phỏng ESP32 trên Wokwi
1. Mở link Wokwi ở đầu trang (hoặc tạo project ESP32 mới rồi chép `sketch.ino`, `diagram.json`).
2. Tạo tệp `libraries.txt` với nội dung:
   ```
   PubSubClient
   DHT sensor library for ESPx
   ```
3. Bấm **Start Simulation**. Serial Monitor sẽ hiện log `[WIFI]`, `[MQTT]`, `[SENSOR][CORE 1]`, `[MQTT][CORE 0]`.
4. Bấm vào DHT22 và kéo thanh trượt **Humidity / Temperature** để thử các vùng của thuật toán.

> Code dùng Arduino-ESP32 **core 3.x** (`ledcAttach`, `ledcWrite(pin, duty)`).

### 2. Mở giao diện web
- Mở trực tiếp `index.html` bằng trình duyệt (cần Internet để tải Chart.js, MQTT.js), **hoặc**
- Triển khai bằng **GitHub Pages**: `Settings → Pages → Deploy from a branch → main / (root)`; truy cập `https://<user>.github.io/<repo>/`.

### 3. Thử nghiệm
| Thao tác | Kết quả |
|----------|---------|
| Đặt độ ẩm 38 % | Phun sương 25 % trong 10 s, bơm bật |
| Đặt độ ẩm 33 % | Phun sương 50 % trong 20 s |
| Đặt độ ẩm 25 % | Phun sương 100 % trong 30 s |
| Đặt độ ẩm ≥ 60 % | Tắt bơm và phun sương |
| Nhiệt độ > 35 °C | Bật quạt (tắt khi < 30 °C) |
| Bấm **MANUAL** → ON/OFF | Điều khiển trực tiếp từng thiết bị |

## 🖼️ Hình ảnh

<!-- Thêm ảnh vào docs/images/ rồi bỏ comment -->
<!-- ![Wokwi](docs/images/wokwi.png) -->
<!-- ![Dashboard](docs/images/dashboard.png) -->

## 🛠️ Xử lý sự cố

- **Web hiện “MQTT Disconnected”**: kiểm tra Internet; trang HTTPS bắt buộc dùng `wss://` (đã cấu hình sẵn).
- **Web không nhận dữ liệu**: hai bên phải dùng **cùng tiền tố topic**; thử đổi tiền tố nếu bị trùng với người khác trên broker công cộng.
- **Lỗi biên dịch `ledcAttach`**: cần Arduino-ESP32 core 3.x; với core 2.x dùng `ledcSetup` + `ledcAttachPin`.
- **Nút ON/OFF bị mờ**: hãy bấm **MANUAL** trước.

## 📌 Hạn chế & hướng phát triển

- Máy phun sương chạy hết chu kỳ rồi bắt đầu chu kỳ mới nếu độ ẩm vẫn thấp; nên thêm thời gian nghỉ giữa các chu kỳ.
- Thiết bị thật cần relay/MOSFET và nguồn riêng; dùng broker riêng có xác thực/TLS.
- Có thể thêm lưu lịch sử (InfluxDB/Grafana) và cảnh báo qua Telegram.

## 📚 Công nghệ

ESP32 • FreeRTOS • PubSubClient • DHTesp • MQTT (EMQX public broker) • MQTT.js • Chart.js • Wokwi

## 👤 Tác giả

Nguyễn Tuấn Anh – Môn Hệ thống nhúng, Trường Đại học Giao thông Vận tải – Giảng viên hướng dẫn: Ths. Võ Quang Sơn
