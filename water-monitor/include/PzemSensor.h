#pragma once

#include "ISensor.h"
#include <HardwareSerial.h>
#include <freertos/semphr.h>

class PzemSensor : public ISensor {
public:
    /// @param serial  HardwareSerial dùng chung (UART2) — đã begin() sẵn
    /// @param busMutex  Mutex bảo vệ bus RS485
    /// @param modbusId  Địa chỉ Modbus slave (mặc định = 1)
    PzemSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId);

    void begin() override;
    void read(SensorReading& reading) override;

private:
    HardwareSerial&   _serial;
    SemaphoreHandle_t _mutex;
    int               _modbusId;

    size_t   modbusTransact(const uint8_t* cmd, size_t cmdLen,
                            uint8_t* rsp, size_t rspLen);
    uint16_t crc16(const uint8_t* buf, size_t len);
};
