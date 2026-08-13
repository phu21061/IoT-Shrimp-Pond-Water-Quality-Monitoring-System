// ============================================================
//  status_led.cpp — LED trạng thái v2.2
//  GPIO4 — Pattern theo README mục 4.12:
//    WiFi lỗi       → nháy chậm 1s
//    Firebase lỗi   → nháy nhanh 0.5s
//    Cả hai lỗi     → nháy liên tục (100ms)
//    Tất cả OK      → sáng liên tục
//    AP Config      → double flash mỗi 2s
// ============================================================

#include "status_led.h"
#include "config.h"
#include "wifi_manager.h"

static const char* TAG = "LED";

namespace StatusLed {

static SystemState  _state        = SystemState::BOOTING;
static uint32_t     _lastMs       = 0;
static bool         _ledOn        = false;
static bool         _firebaseOk   = false;

void begin() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    LOG_D(TAG, "Status LED on pin %d", STATUS_LED_PIN);
}

void setState(SystemState s) {
    _state = s;
}

void setFirebaseReady(bool ready) {
    _firebaseOk = ready;
}

static void ledOn()  { digitalWrite(STATUS_LED_PIN, HIGH); _ledOn = true;  }
static void ledOff() { digitalWrite(STATUS_LED_PIN, LOW);  _ledOn = false; }

void tick() {
    uint32_t now = millis();
    bool wifiOk = WifiMgr::isConnected();

    // OTA in progress — triple flash nhanh mỗi 1.5s
    if (_state == SystemState::OTA_IN_PROGRESS) {
        static uint8_t otaPhase = 0;
        static const uint16_t otaPattern[] = {80, 80, 80, 80, 80, 1100};
        if (now - _lastMs > otaPattern[otaPhase]) {
            _lastMs = now;
            if (otaPhase % 2 == 0) ledOn(); else ledOff();
            otaPhase = (otaPhase + 1) % 6;
        }
        return;
    }

    // AP Config mode — double flash mỗi 2s
    SystemState ws = WifiMgr::state();
    if (ws == SystemState::AP_CONFIG) {
        static uint8_t phase = 0;
        static const uint16_t pattern[] = {50, 100, 50, 1800};
        if (now - _lastMs > pattern[phase]) {
            _lastMs = now;
            if (phase % 2 == 0) ledOn(); else ledOff();
            phase = (phase + 1) % 4;
        }
        return;
    }

    // Tất cả OK → sáng liên tục
    if (wifiOk && _firebaseOk) {
        ledOn();
        return;
    }

    // Cả hai lỗi → nháy liên tục rất nhanh (100ms)
    if (!wifiOk && !_firebaseOk) {
        if (now - _lastMs > 100) {
            _ledOn ? ledOff() : ledOn();
            _lastMs = now;
        }
        return;
    }

    // Chỉ WiFi lỗi → nháy chậm 1s
    if (!wifiOk) {
        if (now - _lastMs > 1000) {
            _ledOn ? ledOff() : ledOn();
            _lastMs = now;
        }
        return;
    }

    // Chỉ Firebase lỗi → nháy 0.5s
    if (!_firebaseOk) {
        if (now - _lastMs > 500) {
            _ledOn ? ledOff() : ledOn();
            _lastMs = now;
        }
        return;
    }
}

} // namespace StatusLed
