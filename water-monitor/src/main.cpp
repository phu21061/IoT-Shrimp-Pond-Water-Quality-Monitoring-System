// ============================================================
//  main.cpp  v2.2 — Smart Water Monitoring System (Refactored)
//  3 cảm biến RS485 Modbus RTU (pH, DO, PZEM) chung 1 bus
//  busMutex + debounce + offline buffer + heartbeat
// ============================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "esp_mac.h"
#include <ArduinoJson.h>

#include "config.h"
#include "types.h"
#include "status_led.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "firebase_client.h"
#include "alert_manager.h"
#include "PhSensor.h"
#include "DoSensor.h"
#include "PzemSensor.h"
#include "OfflineBuffer.h"
#include "OtaManager.h"

static const char* TAG = "MAIN";
static const uint32_t COMMAND_POLL_MS = 5000UL;

// ── Globals ───────────────────────────────────────────────────
char DEVICE_ID[18] = {0};
char FB_PATH_LATEST[64] = {0};
char FB_PATH_HISTORY[64] = {0};
char FB_PATH_HISTORY_FULL[64] = {0};
char FB_PATH_ALERTS[64] = {0};
char FB_PATH_CONFIG[64] = {0};
char FB_PATH_STATUS[64] = {0};
char FB_PATH_COMMAND[64] = {0};
char FB_PATH_LOGS[64] = {0};
char FB_STORAGE_LOG_PREFIX[64] = {0};

static SystemState _sysState = SystemState::BOOTING;
static RemoteConfig _cfg = {
    10000,      // latestIntervalMs
    1800000,    // historyIntervalMs
    { DEFAULT_PH_MIN, DEFAULT_PH_MAX, DEFAULT_DO_MIN, 0.5f, DEFAULT_CURRENT_MAX }, // thresholds
    { DEFAULT_PH_NEUTRAL_VOLTAGE, DEFAULT_PH_VOLTAGE_SLOPE }, // phCalib
    {0, 0, 0},  // offsets
    true,       // turn_on_relay
    18,         // relay_time
    6,          // relay_time_off
    DEFAULT_DEBOUNCE_COUNT, // debounceCount
    false       // valid
};

// ── Synchronisation primitives ───────────────────────────────
static SemaphoreHandle_t configMutex  = NULL;
static SemaphoreHandle_t stateMutex   = NULL;
static SemaphoreHandle_t busMutex     = NULL; // Bảo vệ bus RS485 (3 cảm biến chung 1 bus)
static QueueHandle_t cloudDataQueue   = NULL;
static QueueHandle_t cloudAlertQueue  = NULL;

// ── Shared RS485 UART ────────────────────────────────────────
static HardwareSerial rs485Serial(RS485_UART_NUM);

// ── Sensor objects ───────────────────────────────────────────
// Khởi tạo muộn (sau khi busMutex được tạo) bằng placement new
static uint8_t phBuf[sizeof(PhSensor)] __attribute__((aligned(4)));
static uint8_t doBuf[sizeof(DoSensor)] __attribute__((aligned(4)));
static uint8_t pzemBuf[sizeof(PzemSensor)] __attribute__((aligned(4)));

static PhSensor*   phSensor   = nullptr;
static DoSensor*   doSensor   = nullptr;
static PzemSensor* pzemSensor = nullptr;

// Cảnh báo điều khiển còi qua RELAY_1_PIN (Chân 5).
AlertManager alertManager(RELAY_1_PIN, ALERT_OUTPUT_DURATION_MS);
FirebaseManager firebaseManager;
OfflineBuffer offlineBuffer(OFFLINE_BUFFER_SIZE);
OtaManager otaManager;

// ── Helpers ───────────────────────────────────────────────────
static void setState(SystemState s) {
    if (stateMutex) xSemaphoreTake(stateMutex, portMAX_DELAY);
    _sysState = s;
    if (stateMutex) xSemaphoreGive(stateMutex);
    StatusLed::setState(s);
}

static void printBanner() {
    Serial.println();
    Serial.println(F("╔═══════════════════════════════════════════════╗"));
    Serial.println(F("║   Smart Water Monitoring System  v2.2         ║"));
    Serial.printf( "║   FW %-8s  Device %-20s║\n", FW_VERSION, DEVICE_ID);
    Serial.println(F("╚═══════════════════════════════════════════════╝"));
    LOG_I(TAG, "Free heap : %u B", ESP.getFreeHeap());
    LOG_I(TAG, "PSRAM size: %u B", ESP.getPsramSize());
    LOG_I(TAG, "Flash size: %u B", ESP.getFlashChipSize());
    Serial.println();
}

static void sysTask(void* pvParameters);
static void measureTask(void* pvParameters);
static void cloudTask(void* pvParameters);

// ── setup() ───────────────────────────────────────────────────
void setup() {
    // Tạo DEVICE_ID từ MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(DEVICE_ID, sizeof(DEVICE_ID), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Tạo Firebase paths
    snprintf(FB_PATH_LATEST,  sizeof(FB_PATH_LATEST),  "/devices/%s/latest",  DEVICE_ID);
    snprintf(FB_PATH_HISTORY, sizeof(FB_PATH_HISTORY), "/devices/%s/update",  DEVICE_ID);
    snprintf(FB_PATH_HISTORY_FULL, sizeof(FB_PATH_HISTORY_FULL), "/devices/%s/history_full", DEVICE_ID);
    snprintf(FB_PATH_ALERTS,  sizeof(FB_PATH_ALERTS),  "/devices/%s/note",    DEVICE_ID);
    snprintf(FB_PATH_CONFIG,         sizeof(FB_PATH_CONFIG),         "/devices/%s/config",  DEVICE_ID);
    snprintf(FB_PATH_STATUS,         sizeof(FB_PATH_STATUS),         "/devices/%s/status",  DEVICE_ID);
    snprintf(FB_PATH_COMMAND,        sizeof(FB_PATH_COMMAND),        "/devices/%s/command", DEVICE_ID);
    snprintf(FB_PATH_LOGS,           sizeof(FB_PATH_LOGS),           "/devices/%s/logs",    DEVICE_ID);
    snprintf(FB_STORAGE_LOG_PREFIX,  sizeof(FB_STORAGE_LOG_PREFIX),  "logs/%s/",            DEVICE_ID);

    Serial.begin(115200);
    delay(500);
    printBanner();

    // Tạo synchronisation primitives
    configMutex  = xSemaphoreCreateMutex();
    stateMutex   = xSemaphoreCreateMutex();
    busMutex     = xSemaphoreCreateMutex();
    cloudDataQueue  = xQueueCreate(20, sizeof(SensorReading));
    cloudAlertQueue = xQueueCreate(10, sizeof(AlertRecord));

    // Watchdog
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    LOG_I(TAG, "WDT armed (%d s)", WDT_TIMEOUT_S);

    // Khởi tạo RS485 UART — dùng chung cho 3 cảm biến
    rs485Serial.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    if (RS485_DE_PIN >= 0) {
        pinMode(RS485_DE_PIN, OUTPUT);
        digitalWrite(RS485_DE_PIN, LOW);
    }
    LOG_I(TAG, "RS485 UART%d: RX=%d TX=%d Baud=%d DE=%d",
          RS485_UART_NUM, RS485_RX_PIN, RS485_TX_PIN, RS485_BAUD, RS485_DE_PIN);

    // Tạo sensor objects (placement new — cần busMutex đã tạo)
    phSensor   = new (phBuf)   PhSensor(rs485Serial, busMutex, PH_MODBUS_ID);
    doSensor   = new (doBuf)   DoSensor(rs485Serial, busMutex, DO_MODBUS_ID);
    pzemSensor = new (pzemBuf) PzemSensor(rs485Serial, busMutex, PZEM_MODBUS_ID);


    StatusLed::begin();
    alertManager.begin();
    setState(SystemState::BOOTING);

    // Khởi tạo cảm biến
    LOG_I(TAG, "Initialising sensors ...");
    phSensor->begin();
    doSensor->begin();
    pzemSensor->begin();

    // WiFi
    setState(SystemState::CONNECTING_WIFI);
    WifiMgr::begin();

    // NTP
    setState(SystemState::SYNCING_TIME);
    TimeSync::sync(15000);

    // Firebase
    firebaseManager.begin();

    LOG_I(TAG, "Waiting for Firebase token ...");
    uint32_t fbWait = millis();
    while (!firebaseManager.isReady() && millis() - fbWait < 12000UL) {
        firebaseManager.maintain();
        delay(250);
    }

    // Auto-config: push config mặc định nếu firmware mới được nạp
    firebaseManager.ensureConfigOnFlash();

    // Fetch config từ Firebase
    firebaseManager.fetchConfig(_cfg);
    phSensor->setOffset(_cfg.offsets.ph);
    doSensor->setOffset(_cfg.offsets.oxi, _cfg.offsets.nhiet_do);
    alertManager.setDebounceCount(_cfg.debounceCount);

    // Ghi trạng thái thiết bị lên Firebase /status
    firebaseManager.updateDeviceStatus();

    // OTA — kiểm tra trạng thái rollback sau boot
    otaManager.begin();

    setState(SystemState::IDLE);

    LOG_I(TAG, "  Latest interval      : %lu ms", _cfg.latestIntervalMs);
    LOG_I(TAG, "  Update interval      : %lu ms", _cfg.updateIntervalMs);
    LOG_I(TAG, "  pH thresholds        : [%.1f, %.1f]", _cfg.thresholds.phMin, _cfg.thresholds.phMax);
    LOG_I(TAG, "  DO min threshold     : %.1f mg/L", _cfg.thresholds.doMin);
    LOG_I(TAG, "  Current thresholds   : [%.1f, %.1f] A", _cfg.thresholds.currentMin, _cfg.thresholds.currentMax);
    LOG_I(TAG, "  Debounce count       : %d", _cfg.debounceCount);

    // Tạo FreeRTOS tasks
    xTaskCreatePinnedToCore(sysTask,     "SysTask",     16384,  NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(measureTask, "MeasureTask", 32768,  NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(cloudTask,   "CloudTask",   32768,  NULL, 2, NULL, 0);

    vTaskDelete(NULL);
}

void loop() {}

// ── sysTask — WDT, LED, alert tick ───────────────────────────
static void sysTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while(1) {
        WifiMgr::maintain();
        firebaseManager.maintain();
        StatusLed::setFirebaseReady(firebaseManager.isReady());
        StatusLed::tick();
        alertManager.tick();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── measureTask — Đọc tuần tự pH → DO → PZEM, đánh giá ngưỡng ──
static void measureTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastMeasureMs = 0; 
    uint32_t lastHistoryMs = 0;
    while(1) {
        esp_task_wdt_reset();
        uint32_t interval, updateInterval;
        xSemaphoreTake(configMutex, portMAX_DELAY);
        interval = _cfg.latestIntervalMs;
        updateInterval = _cfg.updateIntervalMs;
        xSemaphoreGive(configMutex);

        if (lastMeasureMs == 0 || millis() - lastMeasureMs >= interval) {
            lastMeasureMs = millis();
            setState(SystemState::MEASURING);
            LOG_I(TAG, "──── Measurement cycle START ────");

            SensorReading r{};

            // Đọc tuần tự (round-robin) — mỗi sensor tự lock busMutex
            // Thêm delay 10ms giữa các lần đọc để đảm bảo thời gian nghỉ (silent interval) 
            // chuẩn Modbus RTU (tối thiểu 3.5 ký tự ~ 4ms ở 9600 baud)
            phSensor->read(r);
            vTaskDelay(pdMS_TO_TICKS(10));
            
            doSensor->read(r);
            vTaskDelay(pdMS_TO_TICKS(10));
            
            pzemSensor->read(r);

            // Timestamp
            time_t now = time(nullptr);
            r.timestamp = now;
            struct tm ti;
            localtime_r(&now, &ti);
            char sign = (NTP_GMT_OFFSET_S >= 0) ? '+' : '-';
            int offH = abs((int)NTP_GMT_OFFSET_S) / 3600;
            int offM = (abs((int)NTP_GMT_OFFSET_S) % 3600) / 60;
            snprintf(r.isoTimestamp, sizeof(r.isoTimestamp),
                     "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                     ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
                     ti.tm_hour, ti.tm_min, ti.tm_sec, sign, offH, offM);

            // Đánh dấu bản ghi lịch sử
            if (lastHistoryMs == 0 || millis() - lastHistoryMs >= updateInterval) {
                r.isHistory = true;
                lastHistoryMs = millis();
            } else {
                r.isHistory = false;
            }

            // Log tổng hợp
            LOG_I(TAG, "@ %s | pH=%s%.2f | DO=%s%.2f mg/L Sat=%.1f%% T=%.1f°C | I=%s%.3fA P=%.1fW",
                  r.isoTimestamp,
                  r.phValid ? "" : "FAIL:", r.ph,
                  r.doValid ? "" : "FAIL:", r.dissolvedO2, r.saturationO2, r.temperature,
                  r.pzemValid ? "" : "FAIL:", r.current, r.power);

            // Đánh giá ngưỡng cảnh báo (có debounce)
            RemoteConfig localCfg;
            xSemaphoreTake(configMutex, portMAX_DELAY);
            localCfg = _cfg;
            xSemaphoreGive(configMutex);

            // Đồng bộ trạng thái còi báo (được phép bật hay không)
            alertManager.setRelay1Enabled(localCfg.turn_on_relay);

            static uint8_t lastFiredMask = 0;
            uint8_t firedMask = alertManager.evaluate(r, localCfg.thresholds);
            
            if (firedMask != 0) {
                setState(SystemState::ALERT_ACTIVE);
                
                // Chỉ gửi lên Firebase khi có lỗi MỚI xuất hiện (Tránh spam /note mỗi giây)
                if (firedMask != lastFiredMask) {
                    LOG_W(TAG, "Threshold violation mask: 0x%02X", firedMask);
                    AlertRecord rec;
                    rec.mask = firedMask;
                    rec.timestamp = r.timestamp;
                    rec.ph_val = r.phValid ? r.ph : -1.0f;
                    rec.do_val = r.doValid ? r.dissolvedO2 : -1.0f;
                    rec.pzem_val = r.pzemValid ? r.current : -1.0f;
                    xQueueSend(cloudAlertQueue, &rec, 0);
                }
            }
            lastFiredMask = firedMask;

            // Gửi data vào queue cho cloudTask
            if (xQueueSend(cloudDataQueue, &r, 0) != pdPASS) {
                // Queue đầy (cloudTask chưa kịp xử lý hoặc offline) → lưu vào buffer
                offlineBuffer.addReading(r);
                LOG_W(TAG, "Data queue full, saved to offline buffer (%u/%u)",
                      (unsigned)offlineBuffer.getCount(), OFFLINE_BUFFER_SIZE);
            }

            if (!alertManager.isActive()) setState(SystemState::IDLE);

            // OTA rollback validation — đếm healthy cycle
            otaManager.markValidIfHealthy();

            LOG_I(TAG, "Buffer: %u/%u readings. Next measure in %lu s.",
                  (unsigned)offlineBuffer.getCount(), OFFLINE_BUFFER_SIZE,
                  interval / 1000UL);
            LOG_I(TAG, "──── Measurement cycle END ────");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── cloudTask — Upload, config, commands, heartbeat, backfill ──
static void cloudTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastConfigPollMs  = 0;
    uint32_t lastCommandPollMs = 0;

    while(1) {
        esp_task_wdt_reset();
        uint32_t now = millis();
        bool online = WifiMgr::isConnected() && firebaseManager.isReady();

        if (online) {
            // ── Xử lý data từ measureTask ────────────────────
            SensorReading r;
            if (xQueueReceive(cloudDataQueue, &r, 0) == pdPASS) {
                setState(SystemState::UPLOADING);

                // Ghi latest
                bool latestOk  = firebaseManager.setLatest(r);
                bool historyOk = true;

                // Push history nếu có cờ
                if (r.isHistory) {
                    vTaskDelay(pdMS_TO_TICKS(200)); // Giãn cách request
                    bool h1 = firebaseManager.pushHistory(r, false);
                    vTaskDelay(pdMS_TO_TICKS(200)); 
                    bool h2 = firebaseManager.pushHistoryFull(r, false);
                    
                    historyOk = h1 && h2;
                    if (!historyOk) {
                        offlineBuffer.addReading(r);
                    }
                }

                if (!latestOk || !historyOk) {
                    setState(SystemState::ERROR_FIREBASE);
                } else if (!alertManager.isActive()) {
                    setState(SystemState::IDLE);
                }
            }

            // ── Xử lý alerts ─────────────────────────────────
            AlertRecord alert;
            if (xQueueReceive(cloudAlertQueue, &alert, 0) == pdPASS) {
                firebaseManager.sendAlert(alert);
            }


            // ── Config poll ───────────────────────────────────
            if (now - lastConfigPollMs >= CONFIG_POLL_MS) {
                lastConfigPollMs = now;
                RemoteConfig tempCfg;
                bool changed = firebaseManager.fetchConfig(tempCfg);
                if (changed) {
                    xSemaphoreTake(configMutex, portMAX_DELAY);
                    _cfg = tempCfg;
                    xSemaphoreGive(configMutex);
                    phSensor->setOffset(_cfg.offsets.ph);
                    doSensor->setOffset(_cfg.offsets.oxi, _cfg.offsets.nhiet_do);
                    alertManager.setDebounceCount(_cfg.debounceCount);
                    LOG_I(TAG, "Config refreshed. latest=%lu ms, update=%lu ms", _cfg.latestIntervalMs, _cfg.updateIntervalMs);
                }

                // Tự động bật lại Relay 1 nếu đến giờ hẹn
                time_t sysTime = time(nullptr);
                struct tm ti;
                if (localtime_r(&sysTime, &ti) && ti.tm_year > 100) {
                    xSemaphoreTake(configMutex, portMAX_DELAY);
                    bool isEnabled = _cfg.turn_on_relay;
                    uint8_t autoOnHour = _cfg.relay_time;
                    xSemaphoreGive(configMutex);

                    if (!isEnabled && ti.tm_hour == autoOnHour) {
                        LOG_I(TAG, "Auto-enabling Relay 1 at %02d:00", ti.tm_hour);
                        if (firebaseManager.setRelay1Enabled(true)) {
                            xSemaphoreTake(configMutex, portMAX_DELAY);
                            _cfg.turn_on_relay = true;
                            xSemaphoreGive(configMutex);
                        }
                    }

                    // Tự động tắt Relay 1 nếu đến giờ hẹn tắt
                    xSemaphoreTake(configMutex, portMAX_DELAY);
                    uint8_t autoOffHour = _cfg.relay_time_off;
                    isEnabled = _cfg.turn_on_relay; // Cập nhật lại (có thể đã bật ở trên)
                    xSemaphoreGive(configMutex);

                    if (isEnabled && ti.tm_hour == autoOffHour) {
                        LOG_I(TAG, "Auto-disabling Relay 1 at %02d:00", ti.tm_hour);
                        if (firebaseManager.setRelay1Enabled(false)) {
                            xSemaphoreTake(configMutex, portMAX_DELAY);
                            _cfg.turn_on_relay = false;
                            xSemaphoreGive(configMutex);
                        }
                    }
                }
            }

            // ── Command poll ──────────────────────────────────
            if (now - lastCommandPollMs >= COMMAND_POLL_MS) {
                lastCommandPollMs = now;
                char cmd[32] = {};
                if (firebaseManager.pollCommand(cmd, sizeof(cmd))) {
                    LOG_I(TAG, "Executing command: [%s]", cmd);
                    if (strcmp(cmd, "CAL_DO") == 0) {
                        setState(SystemState::MEASURING);
                        CalResult calRes = doSensor->calibrateAir();
                        firebaseManager.ackCommand("CAL_DO",
                            (calRes == CalResult::OK) ? "OK" : "FAIL_MODBUS");
                        setState(alertManager.isActive() ? SystemState::ALERT_ACTIVE
                                                         : SystemState::IDLE);
                    } else if (strcmp(cmd, "OTA") == 0) {
                        // ── OTA Update (Bật kiểm tra an toàn & tín hiệu WiFi) ──
                        if (alertManager.isActive()) {
                            LOG_W(TAG, "OTA deferred: alert is active!");
                            firebaseManager.ackCommand("OTA", "OTA_DEFERRED_ALERT_ACTIVE");
                        } else if (WiFi.RSSI() < -85) {
                            LOG_W(TAG, "OTA deferred: WiFi signal too weak (%d dBm)", WiFi.RSSI());
                            firebaseManager.ackCommand("OTA", "OTA_DEFERRED_WEAK_WIFI");
                        } else {
                            char otaUrl[512] = {};
                            if (firebaseManager.getOtaUrl(otaUrl, sizeof(otaUrl))) {
                                firebaseManager.ackCommand("OTA", "OTA_STARTING");
                                setState(SystemState::OTA_IN_PROGRESS);
                                LOG_I(TAG, "Starting OTA from: %s", otaUrl);
                                if (!otaManager.checkAndPerform(otaUrl)) {
                                    // OTA thất bại (download/flash error)
                                    LOG_E(TAG, "OTA FAILED!");
                                    firebaseManager.ackCommand("OTA", "OTA_FAILED");
                                    setState(alertManager.isActive()
                                             ? SystemState::ALERT_ACTIVE
                                             : SystemState::IDLE);
                                }
                                // Nếu thành công → checkAndPerform() đã reboot,
                                // không bao giờ đến đây.
                            } else {
                                LOG_E(TAG, "OTA URL not found on Firebase!");
                                firebaseManager.ackCommand("OTA", "OTA_NO_URL");
                            }
                        }
                    } else {
                        firebaseManager.ackCommand(cmd, "UNKNOWN_COMMAND");
                    }
                }

            }

            // ── Bù dữ liệu offline (Backfill) ────────────────
            if (offlineBuffer.getCount() > 0) {
                SensorReading br = offlineBuffer.getReading(0);
                LOG_I(TAG, "Backfilling offline data: %s", br.isoTimestamp);
                
                if (firebaseManager.backfillReading(br)) {
                    SensorReading dummy;
                    offlineBuffer.popReading(dummy); // Xóa khỏi buffer sau khi gửi thành công
                    LOG_I(TAG, "Backfill OK. Remaining: %u", offlineBuffer.getCount());
                } else {
                    LOG_W(TAG, "Backfill failed. Keeping in buffer.");
                }
            }

        } else {
            // ── OFFLINE — drain queue vào buffer ──────────────
            SensorReading r;
            while (xQueueReceive(cloudDataQueue, &r, 0) == pdPASS) {
                if (r.isHistory) {
                    offlineBuffer.addReading(r);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
