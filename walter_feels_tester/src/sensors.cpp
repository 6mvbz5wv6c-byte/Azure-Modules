/**
 * @file sensors.cpp
 * @brief Sensor Drivers Implementation
 */

#include "sensors.h"

// =============================================================================
// HDC1080 Implementation
// =============================================================================

HDC1080::HDC1080(TwoWire& wire) : _wire(wire), _initialized(false) {}

bool HDC1080::begin() {
    if (!isConnected()) {
        return false;
    }

    // Reset and configure
    reset();
    delay(15);

    // Verify device ID
    uint16_t devId = getDeviceId();
    uint16_t mfgId = getManufacturerId();

    if (mfgId != HDC1080_MANUFACTURER_ID || devId != HDC1080_DEVICE_ID) {
        Serial.printf("[HDC1080] Wrong ID: MFG=0x%04X DEV=0x%04X\n", mfgId, devId);
        return false;
    }

    _initialized = true;
    return true;
}

bool HDC1080::isConnected() {
    _wire.beginTransmission(HDC1080_I2C_ADDR);
    return (_wire.endTransmission() == 0);
}

void HDC1080::_triggerMeasurement(uint8_t reg) {
    _wire.beginTransmission(HDC1080_I2C_ADDR);
    _wire.write(reg);
    _wire.endTransmission();
    delay(15);  // Conversion time
}

uint16_t HDC1080::_readRegister16(uint8_t reg) {
    _wire.beginTransmission(HDC1080_I2C_ADDR);
    _wire.write(reg);
    _wire.endTransmission();

    delay(10);

    _wire.requestFrom((uint8_t)HDC1080_I2C_ADDR, (uint8_t)2);
    if (_wire.available() >= 2) {
        uint16_t msb = _wire.read();
        uint16_t lsb = _wire.read();
        return (msb << 8) | lsb;
    }
    return 0;
}

void HDC1080::_writeRegister(uint8_t reg, uint16_t value) {
    _wire.beginTransmission(HDC1080_I2C_ADDR);
    _wire.write(reg);
    _wire.write(value >> 8);
    _wire.write(value & 0xFF);
    _wire.endTransmission();
}

float HDC1080::readTemperature() {
    _triggerMeasurement(HDC1080_REG_TEMPERATURE);

    _wire.requestFrom((uint8_t)HDC1080_I2C_ADDR, (uint8_t)2);
    if (_wire.available() >= 2) {
        uint16_t raw = (_wire.read() << 8) | _wire.read();
        return ((float)raw / 65536.0f) * 165.0f - 40.0f;
    }
    return NAN;
}

float HDC1080::readHumidity() {
    _triggerMeasurement(HDC1080_REG_HUMIDITY);

    _wire.requestFrom((uint8_t)HDC1080_I2C_ADDR, (uint8_t)2);
    if (_wire.available() >= 2) {
        uint16_t raw = (_wire.read() << 8) | _wire.read();
        return ((float)raw / 65536.0f) * 100.0f;
    }
    return NAN;
}

bool HDC1080::readBoth(float& temp, float& humidity) {
    // Configure for acquisition mode (read both in sequence)
    _writeRegister(HDC1080_REG_CONFIG, 0x1000);  // MODE bit set
    delay(1);

    _triggerMeasurement(HDC1080_REG_TEMPERATURE);

    _wire.requestFrom((uint8_t)HDC1080_I2C_ADDR, (uint8_t)4);
    if (_wire.available() >= 4) {
        uint16_t rawTemp = (_wire.read() << 8) | _wire.read();
        uint16_t rawHum = (_wire.read() << 8) | _wire.read();

        temp = ((float)rawTemp / 65536.0f) * 165.0f - 40.0f;
        humidity = ((float)rawHum / 65536.0f) * 100.0f;
        return true;
    }
    return false;
}

uint16_t HDC1080::getManufacturerId() {
    return _readRegister16(HDC1080_REG_MANUFACTURER);
}

uint16_t HDC1080::getDeviceId() {
    return _readRegister16(HDC1080_REG_DEVICE_ID);
}

uint32_t HDC1080::getSerialId() {
    uint32_t serial = 0;
    serial = _readRegister16(HDC1080_REG_SERIAL_ID_1);
    serial <<= 16;
    serial |= _readRegister16(HDC1080_REG_SERIAL_ID_2);
    return serial;
}

void HDC1080::setHeater(bool enable) {
    uint16_t config = _readRegister16(HDC1080_REG_CONFIG);
    if (enable) {
        config |= 0x2000;
    } else {
        config &= ~0x2000;
    }
    _writeRegister(HDC1080_REG_CONFIG, config);
}

void HDC1080::reset() {
    _writeRegister(HDC1080_REG_CONFIG, 0x8000);  // Software reset
}

// =============================================================================
// LPS22HB Implementation
// =============================================================================

LPS22HB::LPS22HB(TwoWire& wire) : _wire(wire), _initialized(false) {}

bool LPS22HB::begin() {
    if (!isConnected()) {
        return false;
    }

    uint8_t id = whoAmI();
    if (id != LPS22HB_WHO_AM_I_VALUE) {
        Serial.printf("[LPS22HB] Wrong WHO_AM_I: 0x%02X\n", id);
        return false;
    }

    // Configure: ODR = 25Hz, BDU enabled
    _writeRegister8(LPS22HB_REG_CTRL1, 0x30);

    _initialized = true;
    return true;
}

bool LPS22HB::isConnected() {
    _wire.beginTransmission(LPS22HB_I2C_ADDR);
    return (_wire.endTransmission() == 0);
}

uint8_t LPS22HB::_readRegister8(uint8_t reg) {
    _wire.beginTransmission(LPS22HB_I2C_ADDR);
    _wire.write(reg);
    _wire.endTransmission();

    _wire.requestFrom((uint8_t)LPS22HB_I2C_ADDR, (uint8_t)1);
    if (_wire.available()) {
        return _wire.read();
    }
    return 0;
}

void LPS22HB::_writeRegister8(uint8_t reg, uint8_t value) {
    _wire.beginTransmission(LPS22HB_I2C_ADDR);
    _wire.write(reg);
    _wire.write(value);
    _wire.endTransmission();
}

int32_t LPS22HB::_readPressureRaw() {
    // Trigger one-shot measurement
    uint8_t ctrl2 = _readRegister8(LPS22HB_REG_CTRL2);
    _writeRegister8(LPS22HB_REG_CTRL2, ctrl2 | 0x01);

    // Wait for measurement
    delay(10);

    // Read 3 bytes of pressure data
    int32_t raw = 0;
    raw = _readRegister8(LPS22HB_REG_PRESS_OUT_H);
    raw <<= 8;
    raw |= _readRegister8(LPS22HB_REG_PRESS_OUT_L);
    raw <<= 8;
    raw |= _readRegister8(LPS22HB_REG_PRESS_OUT_XL);

    // Sign extend if negative
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }

    return raw;
}

int16_t LPS22HB::_readTemperatureRaw() {
    int16_t raw = _readRegister8(LPS22HB_REG_TEMP_OUT_H);
    raw <<= 8;
    raw |= _readRegister8(LPS22HB_REG_TEMP_OUT_L);
    return raw;
}

float LPS22HB::readPressure() {
    int32_t raw = _readPressureRaw();
    return (float)raw / 4096.0f;  // hPa
}

float LPS22HB::readTemperature() {
    int16_t raw = _readTemperatureRaw();
    return (float)raw / 100.0f;  // Celsius
}

bool LPS22HB::readBoth(float& pressure, float& temp) {
    pressure = readPressure();
    temp = readTemperature();
    return !isnan(pressure) && !isnan(temp);
}

uint8_t LPS22HB::whoAmI() {
    return _readRegister8(LPS22HB_REG_WHO_AM_I);
}

void LPS22HB::reset() {
    _writeRegister8(LPS22HB_REG_CTRL2, 0x04);  // Software reset
    delay(10);
}

// =============================================================================
// SCD30 Implementation
// =============================================================================

SCD30::SCD30(TwoWire& wire) : _wire(wire), _initialized(false),
                              _co2(0), _temperature(0), _humidity(0) {}

bool SCD30::begin(bool autoCalibrate) {
    if (!isConnected()) {
        return false;
    }

    // Stop any previous measurement
    stopMeasuring();
    delay(100);

    // Set auto calibration
    setAutoCalibration(autoCalibrate);

    // Set measurement interval to 2 seconds
    setMeasurementInterval(2);

    _initialized = true;
    return true;
}

bool SCD30::isConnected() {
    _wire.beginTransmission(SCD30_I2C_ADDR);
    return (_wire.endTransmission() == 0);
}

bool SCD30::_sendCommand(uint16_t cmd) {
    _wire.beginTransmission(SCD30_I2C_ADDR);
    _wire.write(cmd >> 8);
    _wire.write(cmd & 0xFF);
    return (_wire.endTransmission() == 0);
}

bool SCD30::_sendCommand(uint16_t cmd, uint16_t arg) {
    uint8_t data[2] = { (uint8_t)(arg >> 8), (uint8_t)(arg & 0xFF) };
    uint8_t crc = _computeCRC8(data, 2);

    _wire.beginTransmission(SCD30_I2C_ADDR);
    _wire.write(cmd >> 8);
    _wire.write(cmd & 0xFF);
    _wire.write(data[0]);
    _wire.write(data[1]);
    _wire.write(crc);
    return (_wire.endTransmission() == 0);
}

bool SCD30::_readData(uint8_t* data, uint8_t len) {
    _wire.requestFrom((uint8_t)SCD30_I2C_ADDR, len);
    if (_wire.available() < len) {
        return false;
    }
    for (int i = 0; i < len; i++) {
        data[i] = _wire.read();
    }
    return true;
}

uint8_t SCD30::_computeCRC8(uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool SCD30::startMeasuring(uint16_t pressure) {
    return _sendCommand(SCD30_CMD_START_MEASURE, pressure);
}

bool SCD30::stopMeasuring() {
    return _sendCommand(SCD30_CMD_STOP_MEASURE);
}

bool SCD30::dataAvailable() {
    _sendCommand(SCD30_CMD_DATA_READY);
    delay(3);

    uint8_t data[3];
    if (!_readData(data, 3)) {
        return false;
    }

    return (data[1] == 1);
}

bool SCD30::readMeasurement() {
    _sendCommand(SCD30_CMD_READ_MEASUREMENT);
    delay(3);

    uint8_t data[18];
    if (!_readData(data, 18)) {
        return false;
    }

    // Parse CO2 (bytes 0-5)
    uint32_t co2Raw = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                      ((uint32_t)data[3] << 8) | data[4];
    memcpy(&_co2, &co2Raw, sizeof(float));

    // Parse Temperature (bytes 6-11)
    uint32_t tempRaw = ((uint32_t)data[6] << 24) | ((uint32_t)data[7] << 16) |
                       ((uint32_t)data[9] << 8) | data[10];
    memcpy(&_temperature, &tempRaw, sizeof(float));

    // Parse Humidity (bytes 12-17)
    uint32_t humRaw = ((uint32_t)data[12] << 24) | ((uint32_t)data[13] << 16) |
                      ((uint32_t)data[15] << 8) | data[16];
    memcpy(&_humidity, &humRaw, sizeof(float));

    return true;
}

float SCD30::getCO2() { return _co2; }
float SCD30::getTemperature() { return _temperature; }
float SCD30::getHumidity() { return _humidity; }

bool SCD30::setMeasurementInterval(uint16_t seconds) {
    if (seconds < 2) seconds = 2;
    if (seconds > 1800) seconds = 1800;
    return _sendCommand(SCD30_CMD_SET_INTERVAL, seconds);
}

bool SCD30::setAutoCalibration(bool enable) {
    return _sendCommand(SCD30_CMD_SET_AUTO_CAL, enable ? 1 : 0);
}

bool SCD30::setTemperatureOffset(float offset) {
    uint16_t ticks = (uint16_t)(offset * 100);
    return _sendCommand(0x5403, ticks);
}

bool SCD30::setAltitudeCompensation(uint16_t altitude) {
    return _sendCommand(0x5102, altitude);
}

uint16_t SCD30::getFirmwareVersion() {
    _sendCommand(SCD30_CMD_READ_FIRMWARE);
    delay(3);

    uint8_t data[3];
    if (_readData(data, 3)) {
        return ((uint16_t)data[0] << 8) | data[1];
    }
    return 0;
}

void SCD30::reset() {
    _sendCommand(SCD30_CMD_SOFT_RESET);
}

// =============================================================================
// LTC4015 Implementation
// =============================================================================

LTC4015::LTC4015(TwoWire& wire) : _wire(wire), _initialized(false) {}

bool LTC4015::begin() {
    if (!isConnected()) {
        return false;
    }

    // Enable force telemetry to read system status
    enableForceTelemetry(true);

    _initialized = true;
    return true;
}

bool LTC4015::isConnected() {
    _wire.beginTransmission(LTC4015_I2C_ADDR);
    return (_wire.endTransmission() == 0);
}

uint16_t LTC4015::_readRegister16(uint8_t reg) {
    _wire.beginTransmission(LTC4015_I2C_ADDR);
    _wire.write(reg);
    if (_wire.endTransmission() != 0) {
        return 0;
    }

    _wire.requestFrom((uint8_t)LTC4015_I2C_ADDR, (uint8_t)2);
    if (_wire.available() >= 2) {
        uint16_t lsb = _wire.read();
        uint16_t msb = _wire.read();
        return (msb << 8) | lsb;  // LTC4015 is little-endian
    }
    return 0;
}

void LTC4015::_writeRegister16(uint8_t reg, uint16_t value) {
    _wire.beginTransmission(LTC4015_I2C_ADDR);
    _wire.write(reg);
    _wire.write(value & 0xFF);        // LSB first
    _wire.write((value >> 8) & 0xFF); // MSB
    _wire.endTransmission();
}

float LTC4015::readBatteryVoltage() {
    uint16_t raw = _readRegister16(LTC4015_REG_VBAT);
    uint8_t cells = getCellCount();
    if (cells == 0) cells = 1;
    return (float)raw * VBAT_SCALE * cells;
}

float LTC4015::readInputVoltage() {
    uint16_t raw = _readRegister16(LTC4015_REG_VIN);
    return (float)raw * VIN_SCALE;
}

float LTC4015::readSystemVoltage() {
    uint16_t raw = _readRegister16(LTC4015_REG_VSYS);
    return (float)raw * VSYS_SCALE;
}

float LTC4015::readBatteryChargeCurrent() {
    int16_t raw = (int16_t)_readRegister16(LTC4015_REG_IBAT);
    return (float)raw * IBAT_SCALE;
}

float LTC4015::readInputCurrent() {
    uint16_t raw = _readRegister16(LTC4015_REG_IIN);
    return (float)raw * IIN_SCALE;
}

float LTC4015::readDieTemperature() {
    int16_t raw = (int16_t)_readRegister16(LTC4015_REG_DIE_TEMP);
    // Temperature formula: T = raw * 0.0215 - 264.4
    return (float)raw * TEMP_SCALE - 264.4f;
}

uint16_t LTC4015::getChargerState() {
    return _readRegister16(LTC4015_REG_CHARGER_STATE);
}

uint16_t LTC4015::getChargeStatus() {
    return _readRegister16(LTC4015_REG_CHARGE_STATUS);
}

uint16_t LTC4015::getSystemStatus() {
    return _readRegister16(LTC4015_REG_SYSTEM_STATUS);
}

uint8_t LTC4015::getCellCount() {
    uint16_t chemCells = _readRegister16(LTC4015_REG_CHEM_CELLS);
    return (chemCells >> 4) & 0x0F;  // Bits 7:4
}

uint8_t LTC4015::getChemistry() {
    uint16_t chemCells = _readRegister16(LTC4015_REG_CHEM_CELLS);
    return chemCells & 0x0F;  // Bits 3:0
}

void LTC4015::suspendCharging() {
    uint16_t config = _readRegister16(LTC4015_REG_CONFIG_BITS);
    config |= 0x0020;  // Set suspend_charger bit
    _writeRegister16(LTC4015_REG_CONFIG_BITS, config);
}

void LTC4015::resumeCharging() {
    uint16_t config = _readRegister16(LTC4015_REG_CONFIG_BITS);
    config &= ~0x0020;  // Clear suspend_charger bit
    _writeRegister16(LTC4015_REG_CONFIG_BITS, config);
}

void LTC4015::enableMPPT(bool enable) {
    uint16_t config = _readRegister16(LTC4015_REG_CONFIG_BITS);
    if (enable) {
        config |= 0x0008;  // Set mppt_en bit
    } else {
        config &= ~0x0008;
    }
    _writeRegister16(LTC4015_REG_CONFIG_BITS, config);
}

void LTC4015::enableForceTelemetry(bool enable) {
    uint16_t config = _readRegister16(LTC4015_REG_CONFIG_BITS);
    if (enable) {
        config |= 0x0004;  // Set force_telemetry bit
    } else {
        config &= ~0x0004;
    }
    _writeRegister16(LTC4015_REG_CONFIG_BITS, config);
}

void LTC4015::enableCoulombCounter(bool enable) {
    uint16_t config = _readRegister16(LTC4015_REG_CONFIG_BITS);
    if (enable) {
        config |= 0x0010;  // Set en_qcount bit
    } else {
        config &= ~0x0010;
    }
    _writeRegister16(LTC4015_REG_CONFIG_BITS, config);
}

const char* LTC4015::getChargerStateString() {
    uint16_t state = getChargerState();

    // Decode charger state bits
    if (state & 0x0800) return "Charging Complete";
    if (state & 0x0400) return "C/10 Termination";
    if (state & 0x0200) return "NTC Pause";
    if (state & 0x0100) return "Timer Terminate";
    if (state & 0x0080) return "Equalize";
    if (state & 0x0040) return "Absorb";
    if (state & 0x0020) return "CC-CV Charging";
    if (state & 0x0010) return "NTC Pause Hot";
    if (state & 0x0008) return "NTC Pause Cold";
    if (state & 0x0004) return "Precharge";
    if (state & 0x0002) return "CC Charge";
    if (state & 0x0001) return "Bat Short Fault";

    return "Idle/Not Charging";
}

const char* LTC4015::getChemistryString() {
    switch (getChemistry()) {
        case LTC4015_CHEM_LIION:     return "Li-Ion";
        case LTC4015_CHEM_LIFEPO4:   return "LiFePO4";
        case LTC4015_CHEM_LEAD_ACID: return "Lead Acid";
        default:                     return "Unknown";
    }
}
