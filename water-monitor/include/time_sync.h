#pragma once

#include <Arduino.h>

namespace TimeSync {
    bool sync(uint32_t timeoutMs = 10000);

    // Returns true if the system clock has been set (year > 2020).
    bool isValid();

} // namespace TimeSync
