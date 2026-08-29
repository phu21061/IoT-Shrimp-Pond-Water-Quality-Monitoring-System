
#include "alert_manager.h"
#include "config.h"

static const char* TAG = "ALERT";

// Bỏ chu kỳ OFF cố định vì mỗi loại cảm biến có chu kỳ riêng 

AlertManager::AlertManager(int relay1Pin, uint32_t outputDurationMs)
    : _relay1Pin(relay1Pin), _durationMs(outputDurationMs),
      _active(false), _relay1Enabled(true), _currentFiredMask(0),
      _relayState(false), _lastToggleMs(0),
      _debounceCount(DEFAULT_DEBOUNCE_COUNT),
      _phLowCounter(0), _phHighCounter(0), _doLowCounter(0), 
      _currentHighCounter(0), _currentLowCounter(0) {}

inline void AlertManager::_pinWrite(int pin, uint8_t val) {
    if (pin >= 0) digitalWrite(pin, val);
}

void AlertManager::updateRelayPins(bool state) {
    if (_relayState != state) {
        _relayState = state;
        if (_relay1Enabled) {
            _pinWrite(_relay1Pin, state ? HIGH : LOW);
        } else {
            _pinWrite(_relay1Pin, LOW);
        }
        LOG_I(TAG, "Relay OUTPUT %s (Relay1 Enabled: %d)", state ? "ON" : "OFF", _relay1Enabled);
    }
}

void AlertManager::setRelay1Enabled(bool enabled) {
    _relay1Enabled = enabled;
    if (!enabled) {
        _pinWrite(_relay1Pin, LOW); // Tắt ngay lập tức nếu bị disable
    }
}

void AlertManager::begin() {
    if (_relay1Pin >= 0) {
        pinMode(_relay1Pin, OUTPUT);
        digitalWrite(_relay1Pin, LOW); 
    }
    LOG_I(TAG, "Alert outputs: RELAY1=%d", _relay1Pin);
}

void AlertManager::setDebounceCount(uint8_t count) {
    _debounceCount = (count > 0) ? count : 1;
}

uint8_t AlertManager::evaluate(const SensorReading& r, const ThresholdConfig& t) {
    uint8_t fired = 0;
    bool hasCurrentAlert = false;
    bool hasAnyAlert = false;

    // ─── SENSOR FAULT (không cần debounce) ─────
    if (!r.phValid)   fired |= (1u << (uint8_t)AlertType::SENSOR_FAIL_PH);
    if (!r.doValid)   fired |= (1u << (uint8_t)AlertType::SENSOR_FAIL_DO);
    if (!r.pzemValid) fired |= (1u << (uint8_t)AlertType::SENSOR_FAIL_PZEM);
    if (fired != 0) hasAnyAlert = true;

    // ─── VALUE_OUT_OF_RANGE (có debounce) ─────

    if (r.phValid && r.ph < t.phMin) {
        if (++_phLowCounter >= _debounceCount) {
            fired |= (1u << (uint8_t)AlertType::PH_LOW);
            hasAnyAlert = true;
        }
    } else { _phLowCounter = 0; }

    if (r.phValid && r.ph > t.phMax) {
        if (++_phHighCounter >= _debounceCount) {
            fired |= (1u << (uint8_t)AlertType::PH_HIGH);
            hasAnyAlert = true;
        }
    } else { _phHighCounter = 0; }

    if (r.doValid && r.dissolvedO2 < t.doMin) {
        if (++_doLowCounter >= _debounceCount) {
            fired |= (1u << (uint8_t)AlertType::DO_LOW);
            hasAnyAlert = true;
        }
    } else if (r.doValid) { _doLowCounter = 0; }

    if (r.pzemValid && r.current > t.currentMax) {
        if (++_currentHighCounter >= _debounceCount) {
            fired |= (1u << (uint8_t)AlertType::CURRENT_HIGH);
            hasAnyAlert = true;
            hasCurrentAlert = true;
        }
    } else if (r.pzemValid) { _currentHighCounter = 0; }

    if (r.pzemValid && r.current < t.currentMin) {
        if (++_currentLowCounter >= _debounceCount) {
            fired |= (1u << (uint8_t)AlertType::CURRENT_LOW);
            hasAnyAlert = true;
            hasCurrentAlert = true;
        }
    } else if (r.pzemValid) { _currentLowCounter = 0; }

    // ─── Xử lý trạng thái ──────────────────────
    if (hasAnyAlert) {
        if (!_active) {
            // Bắt đầu một cảnh báo mới, lập tức bật relay
            _active = true;
            _lastToggleMs = millis();
            updateRelayPins(true);
        }
        _currentFiredMask = fired;
    } else {
        // Hết lỗi -> tắt cảnh báo
        if (_active) {
            _active = false;
            _currentFiredMask = 0;
            updateRelayPins(false);
            LOG_I(TAG, "All parameters normal. Alert cleared.");
        }
    }

    return fired;
}

void AlertManager::tick() {
    if (!_active || _currentFiredMask == 0) return;

    // Phân loại cảnh báo
    bool isDoAlert = (_currentFiredMask & (1u << (uint8_t)AlertType::DO_LOW)) ||
                     (_currentFiredMask & (1u << (uint8_t)AlertType::SENSOR_FAIL_DO));

    bool isCurrentAlert = (_currentFiredMask & (1u << (uint8_t)AlertType::CURRENT_LOW)) ||
                          (_currentFiredMask & (1u << (uint8_t)AlertType::CURRENT_HIGH)) ||
                          (_currentFiredMask & (1u << (uint8_t)AlertType::SENSOR_FAIL_PZEM));

    // Chu kỳ bật/tắt theo loại cảnh báo (ưu tiên: DO > Dòng > pH)
    uint32_t cycleOnMs, cycleOffMs;

    if (isDoAlert) {
        cycleOnMs  = 10000;  // DO: bật 10s
        cycleOffMs = 10000;  //      tắt 10s
    } else if (isCurrentAlert) {
        cycleOnMs  = 3000;   // Dòng: bật 3s
        cycleOffMs = 10000;  //       tắt 10s
    } else {
        cycleOnMs  = 5000;   // pH: bật 5s
        cycleOffMs = 10000;  //     tắt 10s
    }

    uint32_t now = millis();

    if (_relayState) { // Đang ON
        if (now - _lastToggleMs >= cycleOnMs) {
            _lastToggleMs = now;
            updateRelayPins(false); // Chuyển sang OFF
        }
    } else { // Đang OFF
        if (now - _lastToggleMs >= cycleOffMs) {
            _lastToggleMs = now;
            updateRelayPins(true); // Chuyển sang ON
        }
    }
}

bool AlertManager::isActive() const { return _active; }

void AlertManager::clearOutputs() { 
    _active = false;
    _currentFiredMask = 0;
    updateRelayPins(false); 
}
