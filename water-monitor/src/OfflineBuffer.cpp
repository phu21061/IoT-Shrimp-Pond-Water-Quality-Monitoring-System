// ============================================================
//  OfflineBuffer.cpp — Circular buffer trong PSRAM (Thread-safe)
// ============================================================

#include "OfflineBuffer.h"
#include "config.h"
#include <esp_heap_caps.h>

static const char* TAG = "BUFFER";

OfflineBuffer::OfflineBuffer(size_t capacity)
    : _buffer(nullptr), _capacity(capacity), _head(0), _tail(0), _count(0) {
    
    _mutex = xSemaphoreCreateMutex();

    _buffer = (SensorReading*)heap_caps_calloc(capacity, sizeof(SensorReading), MALLOC_CAP_SPIRAM);

    if (!_buffer) {
        _buffer = (SensorReading*)calloc(capacity, sizeof(SensorReading));
        LOG_W(TAG, "PSRAM alloc failed, using heap");
    }

    if (_buffer) {
        LOG_I(TAG, "Buffer allocated: %u entries (%u bytes)",
              (unsigned)capacity, (unsigned)(capacity * sizeof(SensorReading)));
    } else {
        LOG_E(TAG, "Buffer allocation FAILED!");
    }
}

OfflineBuffer::~OfflineBuffer() {
    if (_buffer) free(_buffer);
    if (_mutex) vSemaphoreDelete(_mutex);
}

void OfflineBuffer::addReading(const SensorReading& r) {
    if (!_buffer || !_mutex) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    _buffer[_head] = r;
    _head = (_head + 1) % _capacity;

    if (_count < _capacity) {
        _count++;
    } else {
        // Drop-oldest: khi đầy, head ghi đè lên tail, nên tail phải tiến lên 1
        _tail = (_tail + 1) % _capacity;
    }

    LOG_D(TAG, "Buffered reading %u/%u", (unsigned)_count, (unsigned)_capacity);
    xSemaphoreGive(_mutex);
}

size_t OfflineBuffer::getCount() const {
    if (!_mutex) return 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t c = _count;
    xSemaphoreGive(_mutex);
    return c;
}

bool OfflineBuffer::isEmpty() const {
    return getCount() == 0;
}

const SensorReading& OfflineBuffer::getReading(size_t index) const {
    // Lưu ý: Không dùng mutex ở đây vì nó trả về tham chiếu.
    // Việc này an toàn khi chỉ cloudTask gọi getReading và index < getCount().
    size_t actualIdx = (_tail + index) % _capacity;
    return _buffer[actualIdx];
}

bool OfflineBuffer::popReading(SensorReading& out) {
    if (!_buffer || !_mutex) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_count == 0) {
        xSemaphoreGive(_mutex);
        return false; // Rỗng
    }

    out = _buffer[_tail];
    _tail = (_tail + 1) % _capacity;
    _count--;

    xSemaphoreGive(_mutex);
    return true;
}

void OfflineBuffer::clear() {
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _head = 0;
    _tail = 0;
    _count = 0;
    xSemaphoreGive(_mutex);
    LOG_I(TAG, "Buffer cleared.");
}
