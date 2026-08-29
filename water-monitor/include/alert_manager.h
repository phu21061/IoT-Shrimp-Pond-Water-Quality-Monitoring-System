#pragma once

#include "types.h"
#include <Arduino.h>

class AlertManager {
public:
    /// @param relay1Pin  GPIO còi báo (active HIGH)
    /// @param outputDurationMs  Thời gian còi kêu trước khi ngắt (chu kỳ)
    AlertManager(int relay1Pin, uint32_t outputDurationMs);

    void begin();

    /// Đánh giá ngưỡng cảnh báo với debounce
    uint8_t evaluate(const SensorReading& r, const ThresholdConfig& t);

    /// Cài đặt số lần debounce (từ Firebase config)
    void setDebounceCount(uint8_t count);

    /// Cho phép bật/tắt Relay 1 (Còi báo) theo config
    void setRelay1Enabled(bool enabled);

    /// Gọi liên tục để duy trì oscillator nhấp nháy cho relay
    void tick();

    bool isActive() const;
    void clearOutputs();

private:
    int      _relay1Pin;
    uint32_t _durationMs;
    bool     _active;
    bool     _relay1Enabled;
    uint8_t  _currentFiredMask;

    // Oscillator state cho pH/DO (Chu kỳ bật/tắt)
    bool     _relayState;
    uint32_t _lastToggleMs;

    // Debounce counters
    uint8_t _debounceCount;
    uint8_t _phLowCounter;
    uint8_t _phHighCounter;
    uint8_t _doLowCounter;
    uint8_t _currentHighCounter;
    uint8_t _currentLowCounter;

    void updateRelayPins(bool state);
    inline void _pinWrite(int pin, uint8_t val);
};
