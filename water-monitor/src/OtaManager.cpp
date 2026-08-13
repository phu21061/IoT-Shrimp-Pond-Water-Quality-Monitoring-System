// ============================================================
//  OtaManager.cpp — Cập nhật firmware không dây (OTA) v2.0
//  Dùng Arduino HTTPUpdate + WiFiClientSecure (.setInsecure())
//  Tải firmware qua HTTPS (Firebase Storage / HTTP server),
//  tự động follow 301/302/307 redirects, flash vào OTA partition,
//  reboot, và hỗ trợ tự động rollback nếu firmware mới lỗi.
// ============================================================

#include "OtaManager.h"
#include "config.h"

#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

static const char* TAG = "OTA";

OtaManager::OtaManager()
    : _pendingValidation(false)
    , _inProgress(false)
    , _validated(false)
    , _bootTimeMs(0)
    , _healthyCycles(0) {}

// ── begin — Kiểm tra trạng thái OTA sau khi boot ────────────
void OtaManager::begin() {
    _bootTimeMs = millis();

    // Kiểm tra trạng thái partition đang chạy
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        LOG_E(TAG, "Cannot get running partition!");
        return;
    }
    LOG_I(TAG, "Running partition: %s (addr=0x%08x, size=0x%08x)",
          running->label, running->address, running->size);

    // Kiểm tra có đang pending validation không
    esp_ota_img_states_t otaState;
    esp_err_t err = esp_ota_get_state_partition(running, &otaState);

    if (err == ESP_OK && otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        _pendingValidation = true;
        _validated = false;
        LOG_W(TAG, "Firmware is PENDING VALIDATION — rollback possible!");
        LOG_I(TAG, "Will validate after %lu ms uptime and %d healthy cycles",
              (unsigned long)OTA_VALIDATION_PERIOD_MS, OTA_MIN_HEALTHY_CYCLES);
    } else {
        _pendingValidation = false;
        _validated = true;
        LOG_I(TAG, "Firmware is validated (state=%d). OTA rollback not pending.", (int)otaState);
    }

    // Log thông tin OTA partition dự phòng
    const esp_partition_t* nextOta = esp_ota_get_next_update_partition(NULL);
    if (nextOta) {
        LOG_I(TAG, "Next OTA partition: %s (addr=0x%08x, size=0x%08x)",
              nextOta->label, nextOta->address, nextOta->size);
    }
}

// ── markValidIfHealthy — Đếm healthy cycles, mark valid khi đủ ──
void OtaManager::markValidIfHealthy() {
    if (!_pendingValidation || _validated) return;

    _healthyCycles++;
    uint32_t uptime = millis() - _bootTimeMs;

    LOG_D(TAG, "Healthy cycle %d/%d, uptime %lu/%lu ms",
          _healthyCycles, OTA_MIN_HEALTHY_CYCLES,
          (unsigned long)uptime, (unsigned long)OTA_VALIDATION_PERIOD_MS);

    if (_healthyCycles >= OTA_MIN_HEALTHY_CYCLES && uptime >= OTA_VALIDATION_PERIOD_MS) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            _validated = true;
            _pendingValidation = false;
            LOG_I(TAG, "✓ Firmware VALIDATED! Rollback cancelled. "
                  "(cycles=%d, uptime=%lu ms)", _healthyCycles, (unsigned long)uptime);
        } else {
            LOG_E(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s",
                  esp_err_to_name(err));
        }
    }
}

// ── isPendingValidation ──────────────────────────────────────
bool OtaManager::isPendingValidation() const {
    return _pendingValidation && !_validated;
}

// ── isInProgress ─────────────────────────────────────────────
bool OtaManager::isInProgress() const {
    return _inProgress;
}

// ── checkAndPerform — Tải firmware từ URL và flash ───────────
bool OtaManager::checkAndPerform(const char* firmwareUrl) {
    if (!firmwareUrl || strlen(firmwareUrl) == 0) {
        LOG_E(TAG, "Firmware URL is empty!");
        return false;
    }

    if (_inProgress) {
        LOG_W(TAG, "OTA already in progress!");
        return false;
    }

    _inProgress = true;
    LOG_I(TAG, "╔═══════════════════════════════════════════╗");
    LOG_I(TAG, "║         OTA UPDATE STARTING               ║");
    LOG_I(TAG, "╚═══════════════════════════════════════════╝");
    LOG_I(TAG, "URL: %s", firmwareUrl);

    // Callback tiến trình — nuôi Watchdog (WDT) và log tiến trình %
    httpUpdate.onProgress([](int cur, int total) {
        esp_task_wdt_reset();
        static int lastPct = -1;
        int pct = (total > 0) ? (cur * 100 / total) : 0;
        if (pct % 10 == 0 && pct != lastPct) {
            lastPct = pct;
            LOG_I(TAG, "Progress: %d%% (%d/%d bytes)", pct, cur, total);
        }
    });

    // Thử lại tối đa 3 lần nếu sụt WiFi giữa chừng
    const int MAX_OTA_RETRIES = 3;
    for (int attempt = 1; attempt <= MAX_OTA_RETRIES; attempt++) {
        esp_task_wdt_reset();

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(OTA_DOWNLOAD_TIMEOUT_MS / 1000);

        httpUpdate.rebootOnUpdate(true);
        httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        if (attempt > 1) {
            LOG_W(TAG, "OTA Retry attempt %d/%d...", attempt, MAX_OTA_RETRIES);
        }

        t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

        switch (ret) {
            case HTTP_UPDATE_OK:
                LOG_I(TAG, "╔═══════════════════════════════════════════╗");
                LOG_I(TAG, "║     OTA COMPLETE — REBOOTING NOW!         ║");
                LOG_I(TAG, "╚═══════════════════════════════════════════╝");
                return true;

            case HTTP_UPDATE_FAILED:
                LOG_E(TAG, "OTA Attempt %d/%d FAILED Error (%d): %s",
                      attempt, MAX_OTA_RETRIES,
                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                break;

            case HTTP_UPDATE_NO_UPDATES:
                LOG_W(TAG, "OTA NO UPDATES");
                _inProgress = false;
                return false;
        }

        if (attempt < MAX_OTA_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    _inProgress = false;
    return false;
}
