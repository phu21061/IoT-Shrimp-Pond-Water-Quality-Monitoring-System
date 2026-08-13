#pragma once
// ============================================================
//  OtaManager.h — Cập nhật firmware không dây (OTA) v1.0
//  Hỗ trợ:
//    - Tải firmware từ HTTP(S) URL (Firebase Storage hoặc server khác)
//    - Flash vào OTA partition (dual partition scheme)
//    - Tự động rollback nếu firmware mới không "healthy" sau X phút
//    - Từ chối OTA khi hệ thống đang cảnh báo (alert active)
// ============================================================

#include <Arduino.h>

class OtaManager {
public:
    OtaManager();

    /// Gọi trong setup() sau khi Firebase init.
    /// Kiểm tra trạng thái OTA partition — nếu firmware mới đang chờ
    /// validation thì bắt đầu đếm thời gian healthy.
    void begin();

    /// Thực hiện OTA: tải firmware từ URL, flash, reboot.
    /// @return true nếu bắt đầu OTA thành công (sẽ reboot),
    ///         false nếu lỗi (URL không hợp lệ, download fail, flash fail).
    bool checkAndPerform(const char* firmwareUrl);

    /// Gọi sau mỗi measurement cycle thành công.
    /// Đếm số chu kỳ healthy; khi đủ điều kiện (uptime > OTA_VALIDATION_PERIOD_MS
    /// VÀ >= OTA_MIN_HEALTHY_CYCLES) → gọi esp_ota_mark_app_valid_cancel_rollback().
    void markValidIfHealthy();

    /// @return true nếu firmware hiện tại đang trong giai đoạn validation
    ///         (chưa được mark valid, có thể rollback bất cứ lúc nào).
    bool isPendingValidation() const;

    /// @return true nếu đang trong quá trình download/flash OTA.
    bool isInProgress() const;

private:
    bool     _pendingValidation;   // Firmware đang chờ validation?
    bool     _inProgress;          // Đang download/flash?
    bool     _validated;           // Đã mark valid?
    uint32_t _bootTimeMs;          // millis() lúc begin()
    uint8_t  _healthyCycles;       // Số chu kỳ đo thành công kể từ boot
};
