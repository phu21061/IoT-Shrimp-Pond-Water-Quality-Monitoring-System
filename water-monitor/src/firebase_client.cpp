// ============================================================
//  firebase_client.cpp — Firebase RTDB Client v2.2
//  Thêm: PZEM fields, tách pushHistory, heartbeat, backfill
// ============================================================

#include "firebase_client.h"
#include "config.h"
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* TAG = "FIREBASE";

FirebaseManager::FirebaseManager() : _ready(false) {}

void FirebaseManager::begin() {
    LOG_I(TAG, "Initialising Firebase v2 ...");

    _fbConfig.api_key      = FIREBASE_API_KEY;
    _fbConfig.database_url = FIREBASE_DATABASE_URL;

    _fbAuth.user.email    = FIREBASE_USER_EMAIL;
    _fbAuth.user.password = FIREBASE_USER_PASSWORD;

    _fbConfig.token_status_callback = tokenStatusCallback;
    _fbConfig.timeout.serverResponse = 12 * 1000;

    Firebase.begin(&_fbConfig, &_fbAuth);
    Firebase.reconnectNetwork(true);

    _fbData.setResponseSize(4096);
    _fbDataCloud.setResponseSize(1024);

    LOG_I(TAG, "Firebase init requested. Auth token pending ...");
}

void FirebaseManager::maintain() {
    _ready = Firebase.ready();
}

bool FirebaseManager::isReady() const {
    return _ready;
}

// ── Helper: Build JSON from SensorReading ────────────────────
void FirebaseManager::buildSensorJson(const SensorReading& r, String& jsonOut, bool isBackfilled, bool includePzem) {
    JsonDocument doc;
    doc["device_id"]     = DEVICE_ID;
    doc["timestamp"]     = r.isoTimestamp;
    doc["unix_time"]     = (long)r.timestamp;

    // pH (RS485)
    doc["ph"]            = r.phValid ? r.ph : -1.0f;
    doc["ph_temperature"]= r.phValid ? r.phTemperature : -1.0f;
    doc["ph_valid"]      = r.phValid;

    // DO (RS485)
    doc["temperature"]   = r.doValid ? r.temperature : -1.0f;
    doc["dissolved_o2"]  = r.doValid ? r.dissolvedO2 : -1.0f;
    doc["saturation_o2"] = r.doValid ? r.saturationO2 : -1.0f;
    doc["do_valid"]      = r.doValid;

    // PZEM-016
    if (includePzem) {
        doc["voltage"]       = r.pzemValid ? r.voltage : -1.0f;
        doc["current"]       = r.pzemValid ? r.current : -1.0f;
        doc["power"]         = r.pzemValid ? r.power : -1.0f;
        doc["energy"]        = r.pzemValid ? r.energy : -1.0f;
        doc["frequency"]     = r.pzemValid ? r.frequency : -1.0f;
        doc["power_factor"]  = r.pzemValid ? r.powerFactor : -1.0f;
        doc["pzem_valid"]    = r.pzemValid;
    }

    doc["fw_version"]    = FW_VERSION;
    doc["uptime_s"]      = (unsigned long)(millis() / 1000);

    if (isBackfilled) {
        doc["is_backfilled"] = true;
    }

    serializeJson(doc, jsonOut);
}

// ── setLatest — Ghi đè /latest ──────────────────────────────
bool FirebaseManager::setLatest(const SensorReading& r) {
    if (!_ready) return false;

    String json;
    buildSensorJson(r, json);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    bool success = true;
    if (Firebase.RTDB.setJSON(&_fbData, FB_PATH_LATEST, &fbJson)) {
        LOG_I(TAG, "Latest updated at %s", r.isoTimestamp);
    } else {
        LOG_E(TAG, "setLatest failed: %s", _fbData.errorReason().c_str());
        success = false;
    }

    _fbData.clear();
    return success;
}

// ── pushHistory — Push vào /update (biểu đồ) ────────────────
bool FirebaseManager::pushHistory(const SensorReading& r, bool isBackfilled) {
    if (!_ready) return false;

    String json;
    // Bỏ qua dữ liệu PZEM khi lưu lịch sử theo yêu cầu
    buildSensorJson(r, json, isBackfilled, false);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    if (Firebase.RTDB.pushJSON(&_fbData, FB_PATH_HISTORY, &fbJson)) {
        LOG_I(TAG, "History pushed at %s%s", r.isoTimestamp,
              isBackfilled ? " (backfilled)" : "");
        return true;
    }

    LOG_E(TAG, "pushHistory failed: %s", _fbData.errorReason().c_str());
    return false;
}

// ── pushHistoryFull — Push full data (bao gồm PZEM) vào /history_full ──
bool FirebaseManager::pushHistoryFull(const SensorReading& r, bool isBackfilled) {
    if (!_ready) return false;

    String json;
    // Bật dữ liệu PZEM bằng cách truyền true
    buildSensorJson(r, json, isBackfilled, true);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    if (Firebase.RTDB.pushJSON(&_fbData, FB_PATH_HISTORY_FULL, &fbJson)) {
        LOG_I(TAG, "History Full pushed at %s%s", r.isoTimestamp,
              isBackfilled ? " (backfilled)" : "");
        return true;
    }

    LOG_E(TAG, "pushHistoryFull failed: %s", _fbData.errorReason().c_str());
    return false;
}

// ── backfillReading — Push 1 bản ghi offline vào /update và /history_full ─────
bool FirebaseManager::backfillReading(const SensorReading& r) {
    bool ok1 = pushHistory(r, true);
    bool ok2 = true;
    if (ok1) {
        // Giãn cách request một chút để tránh kẹt Firebase
        vTaskDelay(pdMS_TO_TICKS(200));
        ok2 = pushHistoryFull(r, true);
    }
    return ok1 && ok2;
}

// ── sendAlert — Push cảnh báo vào /note ──────────────────────
bool FirebaseManager::sendAlert(const AlertRecord& alert) {
    if (!_ready) return false;

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = (long)alert.timestamp;

    String errors = "";
    
    // E1: DO (LDO) đứt dây/mất kết nối
    if (alert.mask & (1 << (uint8_t)AlertType::SENSOR_FAIL_DO)) errors += "E1,";
    
    // E2: pH đứt dây/mất kết nối
    if (alert.mask & (1 << (uint8_t)AlertType::SENSOR_FAIL_PH)) errors += "E2,";
    
    // E3: PZEM đứt dây/mất kết nối
    if (alert.mask & (1 << (uint8_t)AlertType::SENSOR_FAIL_PZEM)) errors += "E3,";

    // Nếu không có lỗi cảm biến nào (chỉ là cảnh báo vượt ngưỡng pH/DO) -> Không ghi log vào nhánh note
    if (errors.length() == 0) {
        return true;
    }

    // Xóa dấu phẩy cuối cùng
    errors.remove(errors.length() - 1);

    doc["errors"] = errors;

    String json;
    serializeJson(doc, json);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    if (Firebase.RTDB.pushJSON(&_fbData, FB_PATH_ALERTS, &fbJson)) {
        LOG_I(TAG, "Alert pushed: %s", errors.c_str());
        return true;
    }
    LOG_E(TAG, "sendAlert failed: %s", _fbData.errorReason().c_str());
    return false;
}

// ── pushDefaultConfig ────────────────────────────────────────
void FirebaseManager::pushDefaultConfig() {
    JsonDocument doc;
    doc["latest_interval_ms"]  = 10000;
    doc["update_interval_ms"]  = 1800000;
    doc["ph_min"]        = DEFAULT_PH_MIN;
    doc["ph_max"]        = DEFAULT_PH_MAX;
    doc["do_min"]        = DEFAULT_DO_MIN;
    doc["current_min"]   = 0.5f;
    doc["current_max"]   = DEFAULT_CURRENT_MAX;
    doc["turn_on_relay"]  = true;
    doc["relay_time"]     = 18;
    doc["relay_time_off"] = 6;
    doc["debounce_count"] = DEFAULT_DEBOUNCE_COUNT;
    doc["ph_neutral_mv"] = DEFAULT_PH_NEUTRAL_VOLTAGE; // tương thích ngược
    doc["ph_slope_mv"]   = DEFAULT_PH_VOLTAGE_SLOPE;   // tương thích ngược
    doc["bu_sai_so"]["pH"]       = 0.0f;
    doc["bu_sai_so"]["oxi"]      = 0.0f;
    doc["bu_sai_so"]["nhiet_do"] = 0.0f;

    String json;
    serializeJson(doc, json);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    if (Firebase.RTDB.setJSON(&_fbDataCloud, FB_PATH_CONFIG, &fbJson)) {
        LOG_I(TAG, "Default config pushed.");
    } else {
        LOG_E(TAG, "pushDefaultConfig failed: %s", _fbDataCloud.errorReason().c_str());
    }
    _fbDataCloud.clear();
}

// ── fetchConfig ──────────────────────────────────────────────
bool FirebaseManager::fetchConfig(RemoteConfig& cfg) {
    if (!_ready) return false;

    if (!Firebase.RTDB.getJSON(&_fbDataCloud, FB_PATH_CONFIG)) {
        String err = _fbDataCloud.errorReason();
        _fbDataCloud.clear();
        if (err.indexOf("path not exist") != -1) {
            pushDefaultConfig();
        }
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _fbDataCloud.jsonString());
    _fbDataCloud.clear();
    if (err) return false;

    uint32_t newLatestInterval  = doc["latest_interval_ms"] | 10000;
    uint32_t newUpdateInterval  = doc["update_interval_ms"] | 1800000;

    float newPhMin      = doc["ph_min"]      | DEFAULT_PH_MIN;
    float newPhMax      = doc["ph_max"]      | DEFAULT_PH_MAX;
    float newDoMin      = doc["do_min"]      | DEFAULT_DO_MIN;
    float newCurrentMin = doc["current_min"] | 0.5f;
    float newCurrentMax = doc["current_max"] | DEFAULT_CURRENT_MAX;
    
    bool newTurnOnRelay      = doc["turn_on_relay"] | true;
    uint8_t newRelayTime     = doc["relay_time"] | 18;
    uint8_t newRelayTimeOff  = doc["relay_time_off"] | 6;

    float newPhNeutral  = doc["ph_neutral_mv"] | DEFAULT_PH_NEUTRAL_VOLTAGE;
    float newPhSlope    = doc["ph_slope_mv"]   | DEFAULT_PH_VOLTAGE_SLOPE;

    float newPhOffset   = doc["bu_sai_so"]["pH"]       | 0.0f;
    float newDoOffset   = doc["bu_sai_so"]["oxi"]      | 0.0f;
    float newTempOffset = doc["bu_sai_so"]["nhiet_do"] | 0.0f;

    uint8_t newDebounce = doc["debounce_count"] | (uint8_t)DEFAULT_DEBOUNCE_COUNT;

    bool changed = !cfg.valid
        || cfg.latestIntervalMs        != newLatestInterval
        || cfg.updateIntervalMs        != newUpdateInterval
        || cfg.thresholds.phMin        != newPhMin
        || cfg.thresholds.phMax        != newPhMax
        || cfg.thresholds.doMin        != newDoMin
        || cfg.thresholds.currentMin   != newCurrentMin
        || cfg.thresholds.currentMax   != newCurrentMax
        || cfg.turn_on_relay           != newTurnOnRelay
        || cfg.relay_time              != newRelayTime
        || cfg.relay_time_off          != newRelayTimeOff
        || cfg.phCalib.neutralVoltage  != newPhNeutral
        || cfg.phCalib.voltageSlope    != newPhSlope
        || cfg.offsets.ph              != newPhOffset
        || cfg.offsets.oxi             != newDoOffset
        || cfg.offsets.nhiet_do        != newTempOffset
        || cfg.debounceCount           != newDebounce;

    cfg.latestIntervalMs         = newLatestInterval;
    cfg.updateIntervalMs         = newUpdateInterval;
    cfg.thresholds.phMin         = newPhMin;
    cfg.thresholds.phMax         = newPhMax;
    cfg.thresholds.doMin         = newDoMin;
    cfg.thresholds.currentMin    = newCurrentMin;
    cfg.thresholds.currentMax    = newCurrentMax;
    cfg.turn_on_relay            = newTurnOnRelay;
    cfg.relay_time               = newRelayTime;
    cfg.relay_time_off           = newRelayTimeOff;
    cfg.phCalib.neutralVoltage   = newPhNeutral;
    cfg.phCalib.voltageSlope     = newPhSlope;
    cfg.offsets.ph               = newPhOffset;
    cfg.offsets.oxi              = newDoOffset;
    cfg.offsets.nhiet_do         = newTempOffset;
    cfg.debounceCount            = newDebounce;
    cfg.valid                    = true;

    if (changed) {
        LOG_I(TAG, "Config updated: latest=%lums update=%lums pH[%.1f-%.1f] DO_min=%.1f I[%.1f-%.1f]A debounce=%d",
              cfg.latestIntervalMs, cfg.updateIntervalMs, cfg.thresholds.phMin, cfg.thresholds.phMax,
              cfg.thresholds.doMin, cfg.thresholds.currentMin, cfg.thresholds.currentMax, cfg.debounceCount);
        LOG_I(TAG, "  -> pH offset=%.2f  DO offset=%.2f  Temp offset=%.2f",
              cfg.offsets.ph, cfg.offsets.oxi, cfg.offsets.nhiet_do);
    }

    return changed;
}


// ── pollCommand ──────────────────────────────────────────────
bool FirebaseManager::pollCommand(char* cmdOut, size_t cmdMaxLen) {
    if (!_ready || !cmdOut || cmdMaxLen == 0) return false;

    String path = String(FB_PATH_COMMAND) + "/action";
    if (Firebase.RTDB.getString(&_fbDataCloud, path.c_str())) {
        if (_fbDataCloud.dataType() == "string") {
            String action = _fbDataCloud.stringData();
            if (action.length() > 0) {
                strncpy(cmdOut, action.c_str(), cmdMaxLen - 1);
                cmdOut[cmdMaxLen - 1] = '\0';
                return true;
            }
        }
    }
    return false;
}

void FirebaseManager::ackCommand(const char* command, const char* status) {
    if (!_ready) return;

    String resultPath = String(FB_PATH_COMMAND) + "/result";
    Firebase.RTDB.setString(&_fbDataCloud, resultPath.c_str(), status);

    String cmdPath = String(FB_PATH_COMMAND) + "/last_command";
    Firebase.RTDB.setString(&_fbDataCloud, cmdPath.c_str(), command);

    String actionPath = String(FB_PATH_COMMAND) + "/action";
    Firebase.RTDB.deleteNode(&_fbDataCloud, actionPath.c_str());

    LOG_I(TAG, "Command '%s' ack'd: %s", command, status);
}


// ── setRelay1Enabled ─────────────────────────────────────────
bool FirebaseManager::setRelay1Enabled(bool enabled) {
    if (!_ready) return false;
    String path = String(FB_PATH_CONFIG) + "/turn_on_relay";
    if (Firebase.RTDB.setBool(&_fbDataCloud, path.c_str(), enabled)) {
        LOG_I(TAG, "Relay1 enabled set to %s on Firebase", enabled ? "true" : "false");
        return true;
    }
    LOG_E(TAG, "setRelay1Enabled failed: %s", _fbDataCloud.errorReason().c_str());
    return false;
}

// ── ensureConfigOnFlash — Push config khi firmware mới được nạp ──
bool FirebaseManager::ensureConfigOnFlash() {
    if (!_ready) return false;

    // Đọc FW version đã lưu trên Firebase
    String versionPath = String(FB_PATH_STATUS) + "/fw_version";
    String storedVersion = "";

    if (Firebase.RTDB.getString(&_fbDataCloud, versionPath.c_str())) {
        if (_fbDataCloud.dataType() == "string") {
            storedVersion = _fbDataCloud.stringData();
        }
    }
    _fbDataCloud.clear();

    // So sánh với version hiện tại
    if (storedVersion == FW_VERSION) {
        LOG_I(TAG, "FW version unchanged (%s). Config NOT overwritten.", FW_VERSION);
        return false; // Không cần push config
    }

    // Firmware mới → push config mặc định
    LOG_I(TAG, "New firmware detected! [%s] -> [%s]. Pushing default config...",
          storedVersion.c_str(), FW_VERSION);
    pushDefaultConfig();

    // Cập nhật FW version trên Firebase
    Firebase.RTDB.setString(&_fbDataCloud, versionPath.c_str(), FW_VERSION);
    _fbDataCloud.clear();

    LOG_I(TAG, "Default config pushed. FW version updated to %s", FW_VERSION);
    return true;
}

// ── updateDeviceStatus — Ghi trạng thái thiết bị lên /status ──
void FirebaseManager::updateDeviceStatus() {
    if (!_ready) return;

    JsonDocument doc;
    doc["fw_version"]  = FW_VERSION;
    doc["device_id"]   = DEVICE_ID;
    doc["boot_time"]   = (long)time(nullptr);
    doc["ip"]          = WiFi.localIP().toString();
    doc["rssi"]        = WiFi.RSSI();
    doc["free_heap"]   = ESP.getFreeHeap();
    doc["psram_size"]  = ESP.getPsramSize();

    String json;
    serializeJson(doc, json);

    FirebaseJson fbJson;
    fbJson.setJsonData(json);

    if (Firebase.RTDB.setJSON(&_fbDataCloud, FB_PATH_STATUS, &fbJson)) {
        LOG_I(TAG, "Device status updated on Firebase.");
    } else {
        LOG_E(TAG, "updateDeviceStatus failed: %s", _fbDataCloud.errorReason().c_str());
    }
    _fbDataCloud.clear();
}

// ── getOtaUrl — Đọc URL firmware OTA từ RTDB ────────────────
bool FirebaseManager::getOtaUrl(char* urlOut, size_t maxLen) {
    if (!_ready || !urlOut || maxLen == 0) return false;

    // Đọc /devices/{id}/ota/url
    String basePath = String("/devices/") + DEVICE_ID + "/ota";
    String urlPath = basePath + "/url";

    if (Firebase.RTDB.getString(&_fbDataCloud, urlPath.c_str())) {
        if (_fbDataCloud.dataType() == "string") {
            String url = _fbDataCloud.stringData();
            if (url.length() > 0 && url.length() < maxLen) {
                strncpy(urlOut, url.c_str(), maxLen - 1);
                urlOut[maxLen - 1] = '\0';
                _fbDataCloud.clear();
                LOG_I(TAG, "OTA URL: %s", urlOut);
                return true;
            }
        }
    }
    _fbDataCloud.clear();
    LOG_W(TAG, "OTA URL not found or empty at %s", urlPath.c_str());
    return false;
}
