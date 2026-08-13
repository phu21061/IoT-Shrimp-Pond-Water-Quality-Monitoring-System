// ============================================================
//  wifi_manager.cpp
// ============================================================

#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>   // tzapu/WiFiManager

static const char* TAG = "WIFI";

namespace WifiMgr {

// ── Internal state ───────────────────────────────────────────
static SystemState  _state      = SystemState::BOOTING;
static uint8_t      _retries    = 0;
static uint32_t     _lastTryMs  = 0;

// WiFiManager instance (lives for the duration of begin())
static WiFiManager  _wm;

// ── AP save callback ─────────────────────────────────────────
static void _onSaveConfig() {
    LOG_I(TAG, "Credentials saved via captive portal.");
}

// ── begin() ─────────────────────────────────────────────────
void begin() {
    LOG_I(TAG, "Initialising WiFi (FW %s, Device %s)", FW_VERSION, DEVICE_ID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Vô hiệu hóa chế độ tiết kiệm năng lượng (Modem Sleep) để chống rớt mạng
    WiFi.setHostname(DEVICE_ID);

    _wm.setSaveConfigCallback(_onSaveConfig);
    _wm.setConfigPortalTimeout(AP_TIMEOUT_MS / 1000);
    _wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_MS / 1000);
    _wm.setAPClientCheck(true);   // keep portal open while client connected
    _wm.setTitle("Water Monitor Setup");

    LOG_I(TAG, "Starting WiFiManager autoConnect...");
    LOG_I(TAG, "If no saved AP, portal will open. SSID: '%s' Password: '%s'", AP_SSID, AP_PASSWORD);

    _state = SystemState::CONNECTING_WIFI;

    bool connected = _wm.autoConnect(AP_SSID, AP_PASSWORD);

    if (!connected) {
        LOG_E(TAG, "AP config portal timed out or failed. Rebooting.");
        delay(1000);
        ESP.restart();
    }

    LOG_I(TAG, "Connected! IP: %s  RSSI: %d dBm",
          WiFi.localIP().toString().c_str(), WiFi.RSSI());
    
    _state   = SystemState::IDLE;
    _retries = 0;
}

// ── maintain() ──────────────────────────────────────────────
void maintain() {
    if (WiFi.status() == WL_CONNECTED) {
        _state   = SystemState::IDLE;  // (caller overrides as needed)
        _retries = 0;
        return;
    }

    // Back-off: retry every 10 s
    if (millis() - _lastTryMs < 10000UL) return;
    _lastTryMs = millis();

    _retries++;
    LOG_W(TAG, "WiFi lost. Reconnect attempt %u/%u ...", _retries, WIFI_MAX_RETRIES);
    _state = SystemState::CONNECTING_WIFI;

    WiFi.reconnect();
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 5000UL) delay(200);

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I(TAG, "Reconnected. IP: %s", WiFi.localIP().toString().c_str());
        _state   = SystemState::IDLE;
        _retries = 0;
        return;
    }

    if (_retries >= WIFI_MAX_RETRIES) {
        LOG_E(TAG, "Max WiFi retries reached. Rebooting.");
        delay(500);
        ESP.restart();
    }

    _state = SystemState::ERROR_WIFI;
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

SystemState state() { return _state; }

} // namespace WifiMgr
