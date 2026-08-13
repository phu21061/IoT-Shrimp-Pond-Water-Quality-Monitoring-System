// ============================================================
//  PzemSensor.cpp — Cảm biến dòng điện PZEM-016 (RS485 Modbus RTU)
//  v2.2 — Function Code 0x04 (Input Registers)
//  Đọc 10 thanh ghi: V, I(32-bit), P(32-bit), E(32-bit), F, PF, Alarm
// ============================================================

#include "PzemSensor.h"
#include <Arduino.h>
#include "config.h"

static const char* TAG = "PZEM";

PzemSensor::PzemSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId)
    : _serial(serial), _mutex(busMutex), _modbusId(modbusId) {}

void PzemSensor::begin() {
    LOG_I(TAG, "PZEM-016 Modbus ID=%d  Func=0x%02X  Reg=0x%04X  Count=%d",
          _modbusId, PZEM_FUNC_CODE, PZEM_REG_START, PZEM_REG_COUNT);
}

uint16_t PzemSensor::crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

size_t PzemSensor::modbusTransact(const uint8_t* cmd, size_t cmdLen,
                                   uint8_t* rsp, size_t rspLen) {
    while (_serial.available()) _serial.read();

    if (RS485_DE_PIN >= 0) {
        digitalWrite(RS485_DE_PIN, HIGH);
        delayMicroseconds(200);
    }

    _serial.write(cmd, cmdLen);
    _serial.flush();

    if (RS485_DE_PIN >= 0) {
        delayMicroseconds(200);
        digitalWrite(RS485_DE_PIN, LOW);
    }

    uint32_t deadline = millis() + MODBUS_TIMEOUT_MS;
    size_t rx = 0;
    while (millis() < deadline && rx < rspLen) {
        if (_serial.available()) rsp[rx++] = _serial.read();
    }

    if (rx < rspLen) {
        LOG_W(TAG, "PZEM timeout: want %u got %u", (unsigned)rspLen, (unsigned)rx);
        return 0;
    }

    uint16_t rxCrc = (uint16_t)rsp[rspLen - 1] << 8 | rsp[rspLen - 2];
    uint16_t calcCrc = crc16(rsp, rspLen - 2);
    if (rxCrc != calcCrc) {
        LOG_W(TAG, "PZEM CRC mismatch");
        return 0;
    }
    return rx;
}

void PzemSensor::read(SensorReading& r) {
    // Build Modbus RTU request: Read Input Registers (0x04)
    uint8_t cmd[8];
    cmd[0] = _modbusId;
    cmd[1] = PZEM_FUNC_CODE;  // 0x04
    cmd[2] = (PZEM_REG_START >> 8) & 0xFF;
    cmd[3] = PZEM_REG_START & 0xFF;
    cmd[4] = (PZEM_REG_COUNT >> 8) & 0xFF;
    cmd[5] = PZEM_REG_COUNT & 0xFF;
    uint16_t crc = crc16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    // Response: ID(1) + Func(1) + ByteCount(1) + Data(20) + CRC(2) = 25 bytes
    const size_t rspLen = 25;
    uint8_t rsp[25];

    for (int attempt = 0; attempt < MODBUS_MAX_RETRIES; attempt++) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            LOG_W(TAG, "PZEM busMutex timeout (attempt %d)", attempt + 1);
            continue;
        }

        size_t rx = modbusTransact(cmd, 8, rsp, rspLen);
        xSemaphoreGive(_mutex);

        if (rx == rspLen && rsp[1] == PZEM_FUNC_CODE) {
            // Data bắt đầu từ rsp[3], mỗi register = 2 bytes (High byte, Low byte)

            // Reg0 (0x0000): Điện áp — 16-bit, 0.1V/LSB
            uint16_t vRaw = ((uint16_t)rsp[3] << 8) | rsp[4];
            r.voltage = vRaw / 10.0f;

            // Reg1,2 (0x0001-0x0002): Dòng điện — 32-bit (Low Word trước, High Word sau)
            // PZEM dùng Little Endian word order: Reg1=Low, Reg2=High
            uint16_t iLow  = ((uint16_t)rsp[5] << 8)  | rsp[6];
            uint16_t iHigh = ((uint16_t)rsp[7] << 8)  | rsp[8];
            uint32_t iRaw  = ((uint32_t)iHigh << 16) | (uint32_t)iLow;
            r.current = iRaw / 1000.0f;

            // Reg3,4 (0x0003-0x0004): Công suất — 32-bit, 0.1W/LSB
            uint16_t pLow  = ((uint16_t)rsp[9]  << 8) | rsp[10];
            uint16_t pHigh = ((uint16_t)rsp[11] << 8) | rsp[12];
            uint32_t pRaw  = ((uint32_t)pHigh << 16) | (uint32_t)pLow;
            r.power = pRaw / 10.0f;

            // Reg5,6 (0x0005-0x0006): Điện năng tiêu thụ — 32-bit, 1Wh/LSB
            uint16_t eLow  = ((uint16_t)rsp[13] << 8) | rsp[14];
            uint16_t eHigh = ((uint16_t)rsp[15] << 8) | rsp[16];
            uint32_t eRaw  = ((uint32_t)eHigh << 16) | (uint32_t)eLow;
            r.energy = (float)eRaw;

            // Reg7 (0x0007): Tần số — 16-bit, 0.1Hz/LSB
            uint16_t fRaw = ((uint16_t)rsp[17] << 8) | rsp[18];
            r.frequency = fRaw / 10.0f;

            // Reg8 (0x0008): Hệ số công suất — 16-bit, 0.01/LSB
            uint16_t pfRaw = ((uint16_t)rsp[19] << 8) | rsp[20];
            r.powerFactor = pfRaw / 100.0f;

            // Reg9 (0x0009): Trạng thái cảnh báo — bỏ qua (dùng logic riêng)

            r.pzemValid = true;

            LOG_D(TAG, "V=%.1fV  I=%.3fA  P=%.1fW  E=%.0fWh  F=%.1fHz  PF=%.2f",
                  r.voltage, r.current, r.power, r.energy, r.frequency, r.powerFactor);
            return;
        }

        if (attempt < MODBUS_MAX_RETRIES - 1) {
            LOG_W(TAG, "PZEM read retry %d/%d", attempt + 1, MODBUS_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(MODBUS_RETRY_DELAY_MS));
        }
    }

    LOG_E(TAG, "PZEM sensor FAILED after %d retries", MODBUS_MAX_RETRIES);
    r.pzemValid = false;
}
