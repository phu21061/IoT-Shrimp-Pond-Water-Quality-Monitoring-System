#pragma once
// ============================================================
//  config.h — Cấu hình trung tâm v2.2
//  Hệ thống Giám sát Nước Thông minh (Smart Water Monitoring System)
//
//  3 cảm biến RS485 Modbus RTU chung 1 bus:
//    pH  (RS-PH-N01-3)    Slave ID = 3
//    DO  (RS-LDOS-N01)    Slave ID = 2
//    PZEM (PZEM-016)      Slave ID = 1
//
//  Hãy điền vào TẤT CẢ các giá trị có đánh dấu <- CẤU HÌNH trước khi nạp code.
//  Tất cả các file khác sẽ import file header này; không có giá trị nào
//  bị fix cứng ở nơi khác.
// ============================================================

#include <Arduino.h>
#include "secrets.h"

// ─────────────────────────────────────────────────────────────
//  ĐỊNH DANH FIRMWARE
// ─────────────────────────────────────────────────────────────
#define FW_VERSION "2.2.1"
extern char DEVICE_ID[18]; // Tự động tạo từ địa chỉ MAC lúc khởi động

// ─────────────────────────────────────────────────────────────
//  WiFi — Chế độ Trạm (Station Mode)
// ─────────────────────────────────────────────────────────────
// Thông tin đăng nhập WiFi lưu cố định đã bị xóa bỏ.
// Hệ thống sẽ chỉ sử dụng cổng thông tin (portal) AP của WiFiManager.
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define WIFI_MAX_RETRIES 5

// ─────────────────────────────────────────────────────────────
//  WiFi — Chế độ Phát sóng / Cấu hình (AP Mode)
// ─────────────────────────────────────────────────────────────
// Thông tin cấu hình mạng đã được chuyển vào secrets.h
#define AP_CHANNEL 1
#define AP_MAX_CLIENTS 4
#define AP_TIMEOUT_MS 300000UL // 5 phút

// ─────────────────────────────────────────────────────────────
//  Firebase — Cơ sở dữ liệu thời gian thực (Realtime Database)
// ─────────────────────────────────────────────────────────────
// Firebase credentials đã được chuyển vào secrets.h

// Các đường dẫn RTDB — tất cả dữ liệu nằm trong nhánh /devices/{device_id}/
extern char FB_PATH_LATEST[64];
extern char FB_PATH_HISTORY[64];   // nhánh "update" — dùng cho biểu đồ
extern char FB_PATH_HISTORY_FULL[64]; // nhánh "history_full" — lưu full dữ liệu DO, PZEM
extern char FB_PATH_ALERTS[64];    // nhánh "note"   — thông báo cảnh báo
extern char FB_PATH_CONFIG[64];
extern char FB_PATH_STATUS[64];
extern char FB_PATH_COMMAND[64];
extern char FB_PATH_LOGS[64];      // nhánh "logs"   — batch upload hàng giờ

// Đường dẫn lưu log trên Cloud Storage: logs/{device_id}/YYYY-MM-DD.csv
extern char FB_STORAGE_LOG_PREFIX[64];

// ─────────────────────────────────────────────────────────────
//  NTP / Thời gian
// ─────────────────────────────────────────────────────────────
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_GMT_OFFSET_S 25200L // <- CẤU HÌNH  UTC+7 (Việt Nam)
#define NTP_DST_OFFSET_S 0L

// ─────────────────────────────────────────────────────────────
//  RS485 Bus — Dùng chung cho 3 cảm biến (pH, DO, PZEM)
//  Cả 3 đã được đặt baudrate 9600 (xem address_modbus.txt)
// ─────────────────────────────────────────────────────────────
#define RS485_UART_NUM    2       // UART2 của ESP32-S3
#define RS485_RX_PIN      18     // <- CẤU HÌNH GPIO RX
#define RS485_TX_PIN      17     // <- CẤU HÌNH GPIO TX
#define RS485_BAUD        9600   // Baudrate chung cho toàn bus
#define RS485_DE_PIN      -1     // <- CẤU HÌNH chân DE/RE, -1 nếu dùng auto-direction module

// ─────────────────────────────────────────────────────────────
//  Modbus — Cấu hình chung
// ─────────────────────────────────────────────────────────────
#define MODBUS_TIMEOUT_MS     1000UL  // Thời gian chờ phản hồi (ms)
#define MODBUS_MAX_RETRIES    3       // Retry tối đa trước khi SENSOR_FAULT
#define MODBUS_RETRY_DELAY_MS 50      // Delay giữa các lần retry (ms)

// ─────────────────────────────────────────────────────────────
//  Cảm biến pH — RS-PH-N01-3 (RS485 Modbus RTU)
//  Slave ID = 3 (đã cấu hình, xem address_modbus.txt)
//  Thanh ghi: 0x0000 = pH (uint16 /100), 0x0001 = Nhiệt độ (int16 /10)
// ─────────────────────────────────────────────────────────────
#define PH_MODBUS_ID      3       // <- CẤU HÌNH Slave ID
#define PH_REG_START      0x0000  // Thanh ghi đầu tiên
#define PH_REG_COUNT      2       // pH(1) + nhiệt_độ(1)

// pH Calibration defaults — giữ lại cho tương thích Firebase config
#define DEFAULT_PH_NEUTRAL_VOLTAGE  1448.2f
#define DEFAULT_PH_VOLTAGE_SLOPE    182.9f

// ─────────────────────────────────────────────────────────────
//  Cảm biến Oxy hòa tan (DO) — RS-LDOS-N01-2-20-EX (RS485 Modbus RTU)
//  Slave ID = 2 (đã cấu hình, xem address_modbus.txt)
//  Thanh ghi: 0x0000-0x0001 = Bão hòa (float), 0x0002-0x0003 = DO (float),
//             0x0004-0x0005 = Nhiệt độ (float)
// ─────────────────────────────────────────────────────────────
#define DO_MODBUS_ID      2       // <- CẤU HÌNH Slave ID
#define DO_REG_START      0x0000  // Thanh ghi đầu tiên
#define DO_REG_COUNT      6       // bão_hòa(2) + DO(2) + nhiệt_độ(2)
#define DO_CAL_REG        0x1010  // Thanh ghi kích hoạt hiệu chuẩn
#define DO_CAL_AIR_VALUE  0x0002  // Giá trị ghi để hiệu chuẩn không khí

// ─────────────────────────────────────────────────────────────
//  Cảm biến dòng điện — PZEM-016 (RS485 Modbus RTU)
//  Slave ID = 1 (mặc định, xem address_modbus.txt)
//  Dùng Function Code 0x04 (Input Registers)
//  Thanh ghi 0x0000-0x0009: V, I, P, E, Hz, PF, Alarm
// ─────────────────────────────────────────────────────────────
#define PZEM_MODBUS_ID    1       // <- CẤU HÌNH Slave ID
#define PZEM_REG_START    0x0000  // Thanh ghi đầu tiên
#define PZEM_REG_COUNT    10      // V(1)+I(2)+P(2)+E(2)+F(1)+PF(1)+Alarm(1)
#define PZEM_FUNC_CODE    0x04    // Input Registers (khác 0x03 Holding Registers)

// ─────────────────────────────────────────────────────────────
//  GPIO — Ngõ ra cảnh báo (Relay)
//  Relay CHỈ dùng để bật loa/còi cảnh báo, KHÔNG điều khiển thiết bị ao
// ─────────────────────────────────────────────────────────────
#define RELAY_1_PIN       5       // <- CẤU HÌNH GPIO Relay 1 (active HIGH)
#define STATUS_LED_PIN    4       // <- CẤU HÌNH GPIO LED trạng thái

// Thời gian duy trì relay cảnh báo pH/DO trước khi tự động tắt (ms).
// Dòng điện bất thường → relay đóng liên tục (không dùng timer này).
#define ALERT_OUTPUT_DURATION_MS 60000UL // 60 giây

// ─────────────────────────────────────────────────────────────
//  Lên lịch đo đạc
// ─────────────────────────────────────────────────────────────
#define DEFAULT_INTERVAL_MS 60000UL       // 60 giây
#define MIN_INTERVAL_MS 10000UL           // 10 giây (mức tối thiểu để test)
#define MAX_INTERVAL_MS 86400000UL        // 24 giờ (mức tối đa)
#define CONFIG_POLL_MS 60000UL            // Kiểm tra cấu hình từ xa mỗi 60 giây

// ─────────────────────────────────────────────────────────────
//  Ngưỡng cảnh báo mặc định (Bị ghi đè bởi Firebase /config)
// ─────────────────────────────────────────────────────────────
#define DEFAULT_PH_MIN      6.5f
#define DEFAULT_PH_MAX      8.5f
#define DEFAULT_DO_MIN      4.0f   // mg/L
#define DEFAULT_CURRENT_MAX 15.0f  // A — ngưỡng dòng điện quạt oxy

// ─────────────────────────────────────────────────────────────
//  Debounce cảnh báo (README mục 4.2)
//  Chỉ kích cảnh báo khi vượt ngưỡng liên tục >= N lần đọc
// ─────────────────────────────────────────────────────────────
#define DEFAULT_DEBOUNCE_COUNT 3

// ─────────────────────────────────────────────────────────────
//  Offline Buffer (README mục 4.3)
//  Circular buffer trong PSRAM, drop-oldest khi đầy
// ─────────────────────────────────────────────────────────────
#define OFFLINE_BUFFER_SIZE 48           // Số bản ghi tối đa
#define LOG_UPLOAD_INTERVAL_MS 3600000UL  // Upload batch mỗi 1 giờ

// ─────────────────────────────────────────────────────────────
//  Heartbeat / Presence (thay thế MQTT LWT)
// ─────────────────────────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS 30000UL     // Gửi heartbeat mỗi 30 giây

// ─────────────────────────────────────────────────────────────
//  Watchdog (Bộ định thời giám sát)
// ─────────────────────────────────────────────────────────────
#define WDT_TIMEOUT_S 120 // Thời gian hào phóng để cho phép upload lên Storage

// ─────────────────────────────────────────────────────────────
//  OTA — Cập nhật firmware không dây (Over-The-Air)
//  Firmware .bin được lưu trên Firebase Storage / HTTP server.
//  Trigger: đặt /devices/{id}/command/action = "OTA" trên RTDB.
//  Cơ chế rollback: firmware mới phải chạy healthy đủ thời gian
//  trước khi được mark valid; nếu không → tự động quay lại FW cũ.
// ─────────────────────────────────────────────────────────────
#define OTA_VALIDATION_PERIOD_MS  300000UL   // 5 phút chờ validation sau boot
#define OTA_MIN_HEALTHY_CYCLES   3           // Số chu kỳ đo thành công tối thiểu
#define OTA_DOWNLOAD_TIMEOUT_MS  120000UL    // Timeout download firmware (2 phút)
#define OTA_POLL_INTERVAL_MS     60000UL     // Poll OTA command mỗi 60 giây

// ─────────────────────────────────────────────────────────────
//  Cấu hình in lỗi (Debug / Logging)
// ─────────────────────────────────────────────────────────────
// Các cấp độ: ERROR=1  WARN=2  INFO=3  DEBUG=4  VERBOSE=5
#define LOG_LEVEL 4 // <- CẤU HÌNH (3=cho sản phẩm thật, 4=dành cho lúc code/phát triển)

#define LOG_E(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 1)                                                          \
  Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 2)                                                          \
  Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 3)                                                          \
  Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 4)                                                          \
  Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...)                                                   \
  if (LOG_LEVEL >= 5)                                                          \
  Serial.printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__)
