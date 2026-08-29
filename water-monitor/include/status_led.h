#pragma once

#include "types.h"

namespace StatusLed {

    void begin();
    void setState(SystemState s);

    /// Cập nhật trạng thái Firebase (gọi từ cloudTask)
    void setFirebaseReady(bool ready);

    /// Gọi mỗi giây từ sysTask
    void tick();

}
