// ============================================================
//  time_sync.cpp
// ============================================================

#include "time_sync.h"
#include "config.h"
#include <time.h>

static const char* TAG = "TIME";

namespace TimeSync {

bool sync(uint32_t timeoutMs) {
    LOG_I(TAG, "Syncing NTP: %s / %s  offset=%lds",
          NTP_SERVER_1, NTP_SERVER_2, (long)NTP_GMT_OFFSET_S);

    configTime(NTP_GMT_OFFSET_S, NTP_DST_OFFSET_S, NTP_SERVER_1, NTP_SERVER_2);

    uint32_t start = millis();
    struct tm tmInfo;

    while (!getLocalTime(&tmInfo)) {
        if (millis() - start > timeoutMs) {
            LOG_E(TAG, "NTP sync timed out after %u ms", timeoutMs);
            return false;
        }
        LOG_V(TAG, "Waiting for NTP ...");
        delay(500);
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    LOG_I(TAG, "Time synced: %s (local)", buf);
    return true;
}

bool isValid() {
    time_t now = time(nullptr);
    // Unix epoch for 2021-01-01 = 1609459200
    return now > 1609459200L;
}

} // namespace TimeSync
