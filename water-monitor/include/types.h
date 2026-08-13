#pragma once
// ============================================================
//  types.h — Shared Data Structures  v2.2
//  Cập nhật: thêm PZEM-016, debounce, AlertType mở rộng
// ============================================================

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
//  Sensor reading (one measurement cycle — 3 sensors)
// ─────────────────────────────────────────────────────────────
struct SensorReading {
    // pH (RS485 Modbus — RS-PH-N01-3)
    float   ph;               // Giá trị pH (0-14)
    float   phTemperature;    // Nhiệt độ từ cảm biến pH (°C)
    bool    phValid;          // false nếu cảm biến pH lỗi (SENSOR_FAULT)

    // DO (RS485 Modbus — RS-LDOS-N01)
    float   dissolvedO2;      // mg/L
    float   saturationO2;     // % bão hòa
    float   temperature;      // Nhiệt độ nước từ cảm biến DO (°C) — giá trị chính
    bool    doValid;          // false nếu cảm biến DO lỗi

    // PZEM-016 (RS485 Modbus)
    float   voltage;          // Điện áp (V)
    float   current;          // Dòng điện (A)
    float   power;            // Công suất (W)
    float   energy;           // Điện năng tiêu thụ (Wh)
    float   frequency;        // Tần số (Hz)
    float   powerFactor;      // Hệ số công suất
    bool    pzemValid;        // false nếu PZEM lỗi

    // Timestamp
    time_t  timestamp;        // Unix epoch (UTC)
    char    isoTimestamp[32]; // "2024-06-15T08:30:00+07:00"

    // Điều khiển luồng dữ liệu
    bool    isHistory;        // Đánh dấu bản ghi này dành cho lịch sử (đẩy vào /update)
};

// ─────────────────────────────────────────────────────────────
//  Alert types — mở rộng cho 3 cảm biến
//  Giá trị enum dùng làm bit position trong bitmask (1 << AlertType)
// ─────────────────────────────────────────────────────────────
enum class AlertType : uint8_t {
    NONE = 0,
    PH_LOW,              // 1 — pH dưới ngưỡng min
    PH_HIGH,             // 2 — pH trên ngưỡng max
    DO_LOW,              // 3 — DO dưới ngưỡng min
    CURRENT_HIGH,        // 4 — Dòng điện vượt ngưỡng max
    SENSOR_FAIL_PH,      // 5 — Cảm biến pH mất kết nối/lỗi
    SENSOR_FAIL_DO,      // 6 — Cảm biến DO mất kết nối/lỗi
    SENSOR_FAIL_PZEM,    // 7 — Cảm biến PZEM mất kết nối/lỗi
    CURRENT_LOW,         // 8 — Dòng điện dưới ngưỡng min (chạy không tải/đứt dây)
    ALERT_TYPE_COUNT     // 9 — Sentinel (không dùng trực tiếp)
};

inline const char* alertTypeStr(AlertType t) {
    switch (t) {
        case AlertType::PH_LOW:          return "PH_LOW";
        case AlertType::PH_HIGH:         return "PH_HIGH";
        case AlertType::DO_LOW:          return "DO_LOW";
        case AlertType::CURRENT_HIGH:    return "CURRENT_HIGH";
        case AlertType::SENSOR_FAIL_PH:  return "SENSOR_FAIL_PH";
        case AlertType::SENSOR_FAIL_DO:  return "SENSOR_FAIL_DO";
        case AlertType::SENSOR_FAIL_PZEM:return "SENSOR_FAIL_PZEM";
        case AlertType::CURRENT_LOW:     return "CURRENT_LOW";
        default:                          return "NONE";
    }
}

// Alert record pushed to Firebase (Gộp chung)
struct AlertRecord {
    uint8_t mask;       // Bitmask chứa tất cả các lỗi xảy ra cùng lúc
    float   ph_val;     // E2
    float   do_val;     // E1
    float   pzem_val;   // E3
    time_t  timestamp;
};

// ─────────────────────────────────────────────────────────────
//  Threshold configuration (loaded from Firebase /config)
// ─────────────────────────────────────────────────────────────
struct ThresholdConfig {
    float phMin;
    float phMax;
    float doMin;
    float currentMin;     // Ngưỡng dòng điện tối thiểu (A)
    float currentMax;     // Ngưỡng dòng điện tối đa (A)
};

// ─────────────────────────────────────────────────────────────
//  pH Calibration configuration (loaded from Firebase /config)
//  Giữ lại để tương thích ngược với app — firmware RS485 không sử dụng
// ─────────────────────────────────────────────────────────────
struct PhCalibrationConfig {
    float neutralVoltage;
    float voltageSlope;
};

// ─────────────────────────────────────────────────────────────
//  Sensor offsets (loaded from Firebase /config)
// ─────────────────────────────────────────────────────────────
struct SensorOffsets {
    float ph;
    float oxi;
    float nhiet_do;
};



// ─────────────────────────────────────────────────────────────
//  Remote configuration (full, fetched from Firebase /config)
// ─────────────────────────────────────────────────────────────
struct RemoteConfig {
    uint32_t            latestIntervalMs; // Chu kỳ đo và đẩy /latest (10s)
    uint32_t            updateIntervalMs; // Chu kỳ đẩy /update (30 phút)
    ThresholdConfig     thresholds;       // Ngưỡng cảnh báo
    PhCalibrationConfig phCalib;          // Giữ lại cho tương thích ngược
    SensorOffsets       offsets;          // Bù sai số
    bool                turn_on_relay;    // on/off relay
    uint8_t             relay_time;       // hẹn giờ tự bật
    uint8_t             relay_time_off;   // hẹn giờ tự tắt relay
    uint8_t             debounceCount;    // Số lần liên tiếp trước khi kích cảnh báo
    bool                valid;            // true khi đã fetch thành công
};

// ─────────────────────────────────────────────────────────────
//  System state machine
// ─────────────────────────────────────────────────────────────
enum class SystemState : uint8_t {
    BOOTING,
    AP_CONFIG,
    CONNECTING_WIFI,
    SYNCING_TIME,
    IDLE,
    MEASURING,
    UPLOADING,
    ALERT_ACTIVE,     // threshold violation in progress
    ERROR_WIFI,
    ERROR_FIREBASE,
    ERROR_SENSOR,
    OTA_IN_PROGRESS       // Đang tải firmware OTA
};
