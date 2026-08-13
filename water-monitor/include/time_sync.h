#pragma once
// ============================================================
//  time_sync.h — NTP synchronisation wrapper
//  Fix: include <Arduino.h> so uint32_t is always defined
//  when this header is included standalone.
// ============================================================

#include <Arduino.h>   // provides uint32_t on all ESP32 Arduino targets

namespace TimeSync {

    // Synchronise system clock via NTP. Blocks up to timeoutMs.
    // Returns true when time is valid.
    bool sync(uint32_t timeoutMs = 10000);

    // Returns true if the system clock has been set (year > 2020).
    bool isValid();

} // namespace TimeSync
