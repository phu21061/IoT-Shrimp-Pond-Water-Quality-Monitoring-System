#pragma once
// ============================================================
//  PzemSensor.h — Cảm biến dòng điện PZEM-016 (RS485 Modbus RTU)
//  Slave ID = 1, Baudrate = 9600
//  Function Code 0x04 (Input Registers)
//  Thanh ghi 0x0000-0x0009:
//    0x0000       : Điện áp   (16-bit, 0.1V/LSB)
//    0x0001-0x0002: Dòng điện (32-bit Low+High, 0.001A/LSB)
//    0x0003-0x0004: Công suất (32-bit Low+High, 0.1W/LSB)
//    0x0005-0x0006: Điện năng (32-bit Low+High, 1Wh/LSB)
//    0x0007       : Tần số    (16-bit, 0.1Hz/LSB)
//    0x0008       : Hệ số CS  (16-bit, 0.01/LSB)
//    0x0009       : Cảnh báo  (0xFFFF=có, 0x0000=không)
// ============================================================

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
