#pragma once
// ============================================================
//  wifi_manager.h — WiFi connectivity (STA + AP fallback)
// ============================================================

#include <Arduino.h>
#include "types.h"

namespace WifiMgr {


    void begin();
    void maintain();

    // Returns true when STA is connected and has an IP.
    bool isConnected();

    // Returns current system state for WiFi subsystem.
    SystemState state();

} // namespace WifiMgr
