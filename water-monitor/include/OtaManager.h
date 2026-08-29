#pragma once

#include <Arduino.h>

class OtaManager {
public:
    OtaManager();

    void begin();

    bool checkAndPerform(const char* firmwareUrl);

    void markValidIfHealthy();

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
