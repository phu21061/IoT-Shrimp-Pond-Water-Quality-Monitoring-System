// ============================================================
//  wifi_manager.cpp  — Robust WiFi connectivity (v2.3)
//  Non-blocking reconnect, event-driven, RSSI & heap monitoring
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

// Theo dõi kết nối liên tục
static uint32_t     _lastConnectedMs  = 0;   // Lần cuối thấy WL_CONNECTED
static uint32_t     _lastRssiLogMs    = 0;   // Lần cuối log RSSI
static uint8_t      _consecutiveFails = 0;   // Số lần reconnect thất bại liên tiếp
static bool         _eventDisconnect  = false; // Event flag: WiFi bị ngắt

// Hằng số
static const uint32_t RECONNECT_INTERVAL_MS   = 10000UL;  // Retry mỗi 10 giây
static const uint32_t RSSI_LOG_INTERVAL_MS     = 300000UL; // Log RSSI mỗi 5 phút
static const uint32_t ZOMBIE_TIMEOUT_MS        = 60000UL;  // 60s không connected → force reconnect
static const int      RSSI_WARN_THRESHOLD      = -75;      // dBm
static const uint8_t  MAX_CONSECUTIVE_FAILS    = 3;        // Force disconnect+reconnect sau 3 lần fail

// WiFiManager instance (lives for the duration of begin())
static WiFiManager  _wm;

// ── WiFi Event Handler ───────────────────────────────────────
static void _onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            LOG_W(TAG, "WiFi DISCONNECTED (event). Will auto-reconnect.");
            _eventDisconnect = true;
            _state = SystemState::ERROR_WIFI;
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOG_I(TAG, "WiFi CONNECTED (event).");
            _eventDisconnect = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG_I(TAG, "Got IP: %s  RSSI: %d dBm",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
            _eventDisconnect = false;
            _state = SystemState::IDLE;
            _retries = 0;
            _consecutiveFails = 0;
            _lastConnectedMs = millis();
            break;
        default:
            break;
    }
}

// ── AP save callback ─────────────────────────────────────────
static void _onSaveConfig() {
    LOG_I(TAG, "Credentials saved via captive portal.");
}

// ── begin() ─────────────────────────────────────────────────
void begin() {
    LOG_I(TAG, "Initialising WiFi (FW %s, Device %s)", FW_VERSION, DEVICE_ID);

    // Đăng ký event handler TRƯỚC khi connect
    WiFi.onEvent(_onWiFiEvent);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // Vô hiệu hóa Modem Sleep — chống rớt mạng
    WiFi.setAutoReconnect(true);  // ESP-IDF tự reconnect ở tầng thấp
    WiFi.setHostname(DEVICE_ID);

    // Tối ưu TX power — giảm nhiễu nhưng vẫn đủ mạnh
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

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
    _consecutiveFails = 0;
    _lastConnectedMs = millis();
    _lastRssiLogMs   = millis();
}

// ── maintain() — NON-BLOCKING ───────────────────────────────
void maintain() {
    uint32_t now = millis();

    // ── Đang connected → monitor health ──
    if (WiFi.status() == WL_CONNECTED) {
        _state   = SystemState::IDLE;  // (caller overrides as needed)
        _retries = 0;
        _lastConnectedMs = now;

        // RSSI monitoring định kỳ
        if (now - _lastRssiLogMs >= RSSI_LOG_INTERVAL_MS) {
            _lastRssiLogMs = now;
            int rssi = WiFi.RSSI();
            uint32_t freeHeap = ESP.getFreeHeap();

            if (rssi < RSSI_WARN_THRESHOLD) {
                LOG_W(TAG, "WiFi signal WEAK: %d dBm (threshold: %d dBm). Heap: %u B",
                      rssi, RSSI_WARN_THRESHOLD, freeHeap);
            } else {
                LOG_D(TAG, "WiFi OK: RSSI=%d dBm, Heap=%u B", rssi, freeHeap);
            }

            // Cảnh báo heap thấp
            if (freeHeap < 40000) {
                LOG_W(TAG, "LOW HEAP WARNING: %u bytes free!", freeHeap);
            }
            if (freeHeap < 20000) {
                LOG_E(TAG, "CRITICAL HEAP: %u bytes! Rebooting to prevent crash.", freeHeap);
                delay(500);
                ESP.restart();
            }
        }
        return;
    }

    // ── Mất kết nối — NON-BLOCKING reconnect ──

    // Back-off: retry mỗi RECONNECT_INTERVAL_MS
    if (now - _lastTryMs < RECONNECT_INTERVAL_MS) return;
    _lastTryMs = now;

    _retries++;
    _consecutiveFails++;
    LOG_W(TAG, "WiFi lost. Reconnect attempt %u/%u (consecutive fails: %u)",
          _retries, WIFI_MAX_RETRIES, _consecutiveFails);
    _state = SystemState::CONNECTING_WIFI;

    // Nếu fail liên tiếp nhiều lần → force disconnect trước rồi reconnect
    if (_consecutiveFails >= MAX_CONSECUTIVE_FAILS) {
        LOG_W(TAG, "Too many consecutive fails (%u). Force WiFi.disconnect() + reconnect.",
              _consecutiveFails);
        WiFi.disconnect(false, false);  // disconnect nhưng giữ credentials
        delay(500);  // Chờ WiFi stack reset — chỉ delay ngắn
        _consecutiveFails = 0;
    }

    // NON-BLOCKING reconnect — chỉ gọi WiFi.reconnect(), KHÔNG chờ
    WiFi.reconnect();
    LOG_I(TAG, "WiFi.reconnect() called. Waiting for event callback...");

    // Kiểm tra zombie: connected quá lâu rồi mà giờ mất
    if (_lastConnectedMs > 0 && (now - _lastConnectedMs > ZOMBIE_TIMEOUT_MS)) {
        LOG_W(TAG, "WiFi zombie detected (no connection for %lu ms).",
              now - _lastConnectedMs);
    }

    if (_retries >= WIFI_MAX_RETRIES) {
        LOG_E(TAG, "Max WiFi retries (%u) reached. Rebooting.", WIFI_MAX_RETRIES);
        delay(500);
        ESP.restart();
    }

    _state = SystemState::ERROR_WIFI;
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

SystemState state() { return _state; }

} // namespace WifiMgr
