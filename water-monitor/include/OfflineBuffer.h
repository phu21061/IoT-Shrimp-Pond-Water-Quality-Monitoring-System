

#include "types.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class OfflineBuffer {
public:
    explicit OfflineBuffer(size_t capacity);
    ~OfflineBuffer();

    void addReading(const SensorReading& r);
    size_t getCount() const;
    bool isEmpty() const;
    
    // Lấy bản ghi theo index (0 = cũ nhất)
    const SensorReading& getReading(size_t index) const;

    // Lấy và xóa bản ghi cũ nhất khỏi buffer
    bool popReading(SensorReading& out);

    // Xóa toàn bộ buffer
    void clear();

private:
    SensorReading* _buffer;
    size_t _capacity;
    size_t _head;   // Vị trí ghi tiếp theo
    size_t _tail;   // Vị trí đọc tiếp theo (bản ghi cũ nhất)
    size_t _count;  // Số bản ghi hiện có
    SemaphoreHandle_t _mutex;
};
