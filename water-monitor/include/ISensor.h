#pragma once

#include "types.h"

class ISensor {
public:
    virtual ~ISensor() = default;
    
    // Khởi tạo phần cứng cảm biến
    virtual void begin() = 0;
    
    // Đọc giá trị và điền vào tham chiếu SensorReading
    virtual void read(SensorReading& reading) = 0;
};
