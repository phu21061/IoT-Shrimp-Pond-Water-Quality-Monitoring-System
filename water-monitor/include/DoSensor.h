#pragma once
// ============================================================
//  DoSensor.h — Cảm biến DO RS485 Modbus RTU (RS-LDOS-N01-2-20-EX)
//  Slave ID = 2, Baudrate = 9600
//  Thanh ghi: 0x0000-0x0001 = Bão hòa (float)
//             0x0002-0x0003 = DO mg/L (float)
//             0x0004-0x0005 = Nhiệt độ (float)
// ============================================================

#include "ISensor.h"
#include <HardwareSerial.h>
#include <freertos/semphr.h>

enum class CalResult {
    OK,
    MODBUS_ERROR
};

class DoSensor : public ISensor {
public:
    /// @param serial  HardwareSerial dùng chung (UART2) — đã begin() sẵn
    /// @param busMutex  Mutex bảo vệ bus RS485
    /// @param modbusId  Địa chỉ Modbus slave (mặc định = 2)
    DoSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId);

    void begin() override;
    void read(SensorReading& reading) override;

    void setOffset(float doOffset, float tempOffset);
    CalResult calibrateAir();

private:
    HardwareSerial&   _serial;
    SemaphoreHandle_t _mutex;
    int               _modbusId;

    float _doOffset;
    float _tempOffset;

    size_t   modbusTransact(const uint8_t* cmd, size_t cmdLen,
                            uint8_t* rsp, size_t rspLen);
    uint16_t crc16(const uint8_t* buf, size_t len);
};
