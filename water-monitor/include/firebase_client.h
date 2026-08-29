#pragma once
// ============================================================
//  firebase_client.h — Firebase RTDB Client v2.2
//  Thêm: PZEM fields, heartbeat, backfill offline buffer,
//         tách pushHistory, batch upload
// ============================================================

#include "types.h"
#include <Firebase_ESP_Client.h>

class OfflineBuffer; // forward declaration

class FirebaseManager {
public:
    FirebaseManager();

    void begin();
    void maintain();
    bool isReady() const;

    /// Ghi dữ liệu mới nhất vào /latest (ghi đè)
    bool setLatest(const SensorReading& r);

    /// Push dữ liệu vào /update (nhánh history cho biểu đồ)
    bool pushHistory(const SensorReading& r, bool isBackfilled = false);
    bool pushHistoryFull(const SensorReading& r, bool isBackfilled = false);

    /// Push cảnh báo vào /note
    bool sendAlert(const AlertRecord& alert);

    /// Lấy cấu hình từ /config — trả về true nếu có thay đổi
    bool fetchConfig(RemoteConfig& cfg);

    /// Poll lệnh từ app qua /command/action
    bool pollCommand(char* cmdOut, size_t cmdMaxLen);
    void ackCommand(const char* command, const char* status);


    /// Đặt trạng thái relay1/enabled lên Firebase (Dùng khi tự động bật lúc chạng vạng)
    bool setRelay1Enabled(bool enabled);

    /// Backfill offline buffer — gửi bù dữ liệu đã đệm khi có mạng
    bool backfillReading(const SensorReading& r);

    /// Kiểm tra FW version trên Firebase, push config mặc định nếu firmware mới được nạp
    bool ensureConfigOnFlash();

    /// Ghi trạng thái thiết bị (FW version, IP, RSSI, uptime) lên /status
    void updateDeviceStatus();

    /// Đọc URL firmware OTA từ /devices/{id}/ota/url
    bool getOtaUrl(char* urlOut, size_t maxLen);

private:
    FirebaseData _fbData;
    FirebaseData _fbDataCloud;
    FirebaseAuth _fbAuth;
    FirebaseConfig _fbConfig;
    bool _ready;
    uint8_t _consecutiveErrors;  // Đếm lỗi SSL liên tiếp để trigger recovery

    void pushDefaultConfig();
    void buildSensorJson(const SensorReading& r, String& jsonOut, bool isBackfilled = false, bool includePzem = true);
    void _handleError(FirebaseData& fbData, const char* operation);
};
