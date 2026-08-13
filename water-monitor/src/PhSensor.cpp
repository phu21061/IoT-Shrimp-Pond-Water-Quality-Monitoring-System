// ============================================================
//  PhSensor.cpp — Cảm biến pH RS485 Modbus RTU (RS-PH-N01-3)
//  v2.2 — Chuyển từ Analog ADC sang RS485 Modbus
// ============================================================

#include "PhSensor.h"
#include <Arduino.h>
#include "config.h"

static const char* TAG = "PH_SENSOR";

PhSensor::PhSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId)
    : _serial(serial), _mutex(busMutex), _modbusId(modbusId), _offset(0.0f) {}

void PhSensor::begin() {
    LOG_I(TAG, "pH RS485 Modbus ID=%d  Reg=0x%04X  Count=%d",
          _modbusId, PH_REG_START, PH_REG_COUNT);
}

void PhSensor::setOffset(float offset) {
    _offset = offset;
}

uint16_t PhSensor::crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

size_t PhSensor::modbusTransact(const uint8_t* cmd, size_t cmdLen,
                                 uint8_t* rsp, size_t rspLen) {
    // Xóa buffer nhận trước khi gửi
    while (_serial.available()) _serial.read();

    // Bật chân DE nếu có (auto-direction module thì bỏ qua)
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

    // Chờ nhận phản hồi
    uint32_t deadline = millis() + MODBUS_TIMEOUT_MS;
    size_t rx = 0;
    while (millis() < deadline && rx < rspLen) {
        if (_serial.available()) rsp[rx++] = _serial.read();
    }

    if (rx < rspLen) return 0; // timeout

    // Kiểm tra CRC
    uint16_t rxCrc = (uint16_t)rsp[rspLen - 1] << 8 | rsp[rspLen - 2];
    uint16_t calcCrc = crc16(rsp, rspLen - 2);
    if (rxCrc != calcCrc) return 0; // CRC mismatch

    return rx;
}

void PhSensor::read(SensorReading& r) {
    // Build Modbus RTU request: Read Holding Registers (0x03)
    uint8_t cmd[8];
    cmd[0] = _modbusId;
    cmd[1] = 0x03; // Function: Read Holding Registers
    cmd[2] = (PH_REG_START >> 8) & 0xFF;
    cmd[3] = PH_REG_START & 0xFF;
    cmd[4] = (PH_REG_COUNT >> 8) & 0xFF;
    cmd[5] = PH_REG_COUNT & 0xFF;
    uint16_t crc = crc16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    // Response: ID(1) + Func(1) + ByteCount(1) + Data(4) + CRC(2) = 9 bytes
    const size_t rspLen = 9;
    uint8_t rsp[9];

    for (int attempt = 0; attempt < MODBUS_MAX_RETRIES; attempt++) {
        // Khóa bus RS485 (busMutex) — tối đa 500ms
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            LOG_W(TAG, "pH busMutex timeout (attempt %d)", attempt + 1);
            continue;
        }

        size_t rx = modbusTransact(cmd, 8, rsp, rspLen);
        xSemaphoreGive(_mutex);

        if (rx == rspLen && rsp[1] == 0x03) {
            // Parse pH: thanh ghi 0x0000 — uint16, giá trị thực = value / 100
            uint16_t phRaw = ((uint16_t)rsp[3] << 8) | rsp[4];
            float phValue = phRaw / 100.0f + _offset;

            // Parse nhiệt độ: thanh ghi 0x0001 — int16, giá trị thực = value / 10
            int16_t tempRaw = (int16_t)(((uint16_t)rsp[5] << 8) | rsp[6]);
            float tempValue = tempRaw / 10.0f;

            r.ph = phValue;
            r.phTemperature = tempValue;
            r.phValid = true;

            LOG_D(TAG, "pH=%.2f  T_pH=%.1f°C", r.ph, r.phTemperature);
            return;
        }

        if (attempt < MODBUS_MAX_RETRIES - 1) {
            LOG_W(TAG, "pH read retry %d/%d", attempt + 1, MODBUS_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(MODBUS_RETRY_DELAY_MS));
        }
    }

    LOG_E(TAG, "pH sensor FAILED after %d retries", MODBUS_MAX_RETRIES);
    r.phValid = false;
}
