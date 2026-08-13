#pragma once
// ============================================================
//  status_led.h — Non-blocking LED blink patterns v2.2
//  GPIO4 — phân biệt WiFi / Firebase / cả hai lỗi
// ============================================================

#include "types.h"

namespace StatusLed {

    void begin();
    void setState(SystemState s);

    /// Cập nhật trạng thái Firebase (gọi từ cloudTask)
    void setFirebaseReady(bool ready);

    /// Gọi mỗi giây từ sysTask
    void tick();

} // namespace StatusLed
