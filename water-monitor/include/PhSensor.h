#pragma once

#include "ISensor.h"
#include <HardwareSerial.h>
#include <freertos/semphr.h>

class PhSensor : public ISensor {
public:
    /// @param serial  HardwareSerial dùng chung (UART2) — đã begin() sẵn
    /// @param busMutex  Mutex bảo vệ bus RS485 (3 cảm biến chung 1 bus)
    /// @param modbusId  Địa chỉ Modbus slave (mặc định = 3)
    PhSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId);

    void begin() override;
    void read(SensorReading& reading) override;

    /// Bù sai số pH từ Firebase config (bu_sai_so.pH)
    void setOffset(float offset);

private:
    HardwareSerial&   _serial;
    SemaphoreHandle_t _mutex;
    int               _modbusId;
    float             _offset;

    uint16_t crc16(const uint8_t* buf, size_t len);
    size_t   modbusTransact(const uint8_t* cmd, size_t cmdLen,
                            uint8_t* rsp, size_t rspLen);
};
