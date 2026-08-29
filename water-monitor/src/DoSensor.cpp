#include "DoSensor.h"
#include <Arduino.h>
#include "config.h"
#include <string.h>

static const char* TAG = "DO_SENSOR";

DoSensor::DoSensor(HardwareSerial& serial, SemaphoreHandle_t busMutex, int modbusId)
    : _serial(serial), _mutex(busMutex), _modbusId(modbusId),
      _doOffset(0.0f), _tempOffset(0.0f) {}

void DoSensor::begin() {
    LOG_I(TAG, "DO RS485 Modbus ID=%d  Reg=0x%04X  Count=%d",
          _modbusId, DO_REG_START, DO_REG_COUNT);
}

void DoSensor::setOffset(float doOffset, float tempOffset) {
    _doOffset = doOffset;
    _tempOffset = tempOffset;
}

uint16_t DoSensor::crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

size_t DoSensor::modbusTransact(const uint8_t* cmd, size_t cmdLen,
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
        LOG_W(TAG, "DO timeout: want %u got %u", (unsigned)rspLen, (unsigned)rx);
        return 0;
    }

    uint16_t rxCrc = (uint16_t)rsp[rspLen - 1] << 8 | rsp[rspLen - 2];
    uint16_t calcCrc = crc16(rsp, rspLen - 2);
    if (rxCrc != calcCrc) {
        LOG_W(TAG, "DO CRC mismatch");
        return 0;
    }
    return rx;
}

// Chuyển đổi 32-bit raw (IEEE 754 Big Endian) sang float
static float toFloat(uint32_t raw) {
    float f;
    memcpy(&f, &raw, sizeof(f));
    return f;
}

void DoSensor::read(SensorReading& r) {
    // Build Modbus RTU request: Read Holding Registers (0x03)
    uint8_t cmd[8];
    cmd[0] = _modbusId;
    cmd[1] = 0x03;
    cmd[2] = (DO_REG_START >> 8) & 0xFF;
    cmd[3] = DO_REG_START & 0xFF;
    cmd[4] = (DO_REG_COUNT >> 8) & 0xFF;
    cmd[5] = DO_REG_COUNT & 0xFF;
    uint16_t crc = crc16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    // Response: ID(1) + Func(1) + ByteCount(1) + Data(12) + CRC(2) = 17 bytes
    const size_t rspLen = 17;
    uint8_t rsp[17];

    for (int attempt = 0; attempt < MODBUS_MAX_RETRIES; attempt++) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            LOG_W(TAG, "DO busMutex timeout (attempt %d)", attempt + 1);
            continue;
        }

        size_t rx = modbusTransact(cmd, 8, rsp, rspLen);
        xSemaphoreGive(_mutex);

        if (rx == rspLen && rsp[1] == 0x03) {
            // Parse float values (Big Endian, 2 registers = 4 bytes each)
            uint32_t satRaw  = ((uint32_t)rsp[3]  << 24) | ((uint32_t)rsp[4]  << 16) |
                               ((uint32_t)rsp[5]  << 8)  | (uint32_t)rsp[6];
            uint32_t doRaw   = ((uint32_t)rsp[7]  << 24) | ((uint32_t)rsp[8]  << 16) |
                               ((uint32_t)rsp[9]  << 8)  | (uint32_t)rsp[10];
            uint32_t tempRaw = ((uint32_t)rsp[11] << 24) | ((uint32_t)rsp[12] << 16) |
                               ((uint32_t)rsp[13] << 8)  | (uint32_t)rsp[14];

            r.saturationO2 = toFloat(satRaw) * 100.0f;
            r.dissolvedO2  = toFloat(doRaw)  + _doOffset;
            r.temperature  = toFloat(tempRaw) + _tempOffset;
            r.doValid      = true;

            LOG_D(TAG, "DO=%.2f mg/L  Sat=%.1f%%  T=%.1f°C",
                  r.dissolvedO2, r.saturationO2, r.temperature);
            return;
        }

        if (attempt < MODBUS_MAX_RETRIES - 1) {
            LOG_W(TAG, "DO read retry %d/%d", attempt + 1, MODBUS_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(MODBUS_RETRY_DELAY_MS));
        }
    }

    LOG_E(TAG, "DO sensor FAILED after %d retries", MODBUS_MAX_RETRIES);
    r.doValid = false;
}

CalResult DoSensor::calibrateAir() {
    uint8_t cmd[8];
    cmd[0] = _modbusId;
    cmd[1] = 0x06; // Write Single Register
    cmd[2] = (DO_CAL_REG >> 8) & 0xFF;
    cmd[3] = DO_CAL_REG & 0xFF;
    cmd[4] = (DO_CAL_AIR_VALUE >> 8) & 0xFF;
    cmd[5] = DO_CAL_AIR_VALUE & 0xFF;
    uint16_t crc = crc16(cmd, 6);
    cmd[6] = crc & 0xFF;
    cmd[7] = (crc >> 8) & 0xFF;

    uint8_t rsp[8];

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        LOG_E(TAG, "DO calibration busMutex timeout");
        return CalResult::MODBUS_ERROR;
    }

    size_t rx = modbusTransact(cmd, 8, rsp, sizeof(rsp));
    xSemaphoreGive(_mutex);

    if (rx == sizeof(rsp) && rsp[1] == 0x06) {
        LOG_I(TAG, "DO calibration SUCCESS");
        return CalResult::OK;
    }

    LOG_E(TAG, "DO calibration FAILED");
    return CalResult::MODBUS_ERROR;
}
