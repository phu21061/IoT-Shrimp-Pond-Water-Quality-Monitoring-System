#pragma once
// ============================================================
//  wifi_manager.h — WiFi connectivity (STA + AP fallback)
// ============================================================

#include <Arduino.h>
#include "types.h"

namespace WifiMgr {

    // Call once in setup().
    // Attempts STA connection; on failure opens AP config portal.
    // Blocks until connected or AP timeout expires (then reboots).
    void begin();

    // Non-blocking maintenance — call every loop iteration.
    // Handles reconnection on drop-out.
    void maintain();

    // Returns true when STA is connected and has an IP.
    bool isConnected();

    // Returns current system state for WiFi subsystem.
    SystemState state();

} // namespace WifiMgr
