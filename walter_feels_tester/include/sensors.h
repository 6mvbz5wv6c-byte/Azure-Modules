/**
 * @file sensors.h
 * @brief Sensor Drivers for Walter Feels
 *
 * Drivers for HDC1080 (temp/humidity), LPS22HB (pressure), and SCD30 (CO2)
 */

#ifndef WALTER_FEELS_SENSORS_H
#define WALTER_FEELS_SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// =============================================================================
// HDC1080 - Temperature and Humidity Sensor
// =============================================================================
class HDC1080 {
public:
    HDC1080(TwoWire& wire = Wire);

    bool begin();
    bool isConnected();

    float readTemperature();      // Returns temperature in Celsius
    float readHumidity();         // Returns relative humidity in %
    bool readBoth(float& temp, float& humidity);

    uint16_t getManufacturerId();
    uint16_t getDeviceId();
    uint32_t getSerialId();

    void setHeater(bool enable);
    void reset();

private:
    TwoWire& _wire;
    bool _initialized;

    uint16_t _readRegister16(uint8_t reg);
    void _writeRegister(uint8_t reg, uint16_t value);
    void _triggerMeasurement(uint8_t reg);
};

// =============================================================================
// LPS22HB - Barometric Pressure Sensor
// =============================================================================
class LPS22HB {
public:
    LPS22HB(TwoWire& wire = Wire);

    bool begin();
    bool isConnected();

    float readPressure();         // Returns pressure in hPa (mbar)
    float readTemperature();      // Returns temperature in Celsius
    bool readBoth(float& pressure, float& temp);

    uint8_t whoAmI();
    void reset();

private:
    TwoWire& _wire;
    bool _initialized;

    uint8_t _readRegister8(uint8_t reg);
    void _writeRegister8(uint8_t reg, uint8_t value);
    int32_t _readPressureRaw();
    int16_t _readTemperatureRaw();
};

// =============================================================================
// SCD30 - CO2, Temperature, and Humidity Sensor
// =============================================================================
class SCD30 {
public:
    SCD30(TwoWire& wire = Wire);

    bool begin(bool autoCalibrate = true);
    bool isConnected();

    bool startMeasuring(uint16_t pressure = 0);  // Pressure in mbar, 0 = disabled
    bool stopMeasuring();

    bool dataAvailable();
    bool readMeasurement();

    float getCO2();               // Returns CO2 in ppm
    float getTemperature();       // Returns temperature in Celsius
    float getHumidity();          // Returns relative humidity in %

    bool setMeasurementInterval(uint16_t seconds);
    bool setAutoCalibration(bool enable);
    bool setTemperatureOffset(float offset);
    bool setAltitudeCompensation(uint16_t altitude);

    uint16_t getFirmwareVersion();
    void reset();

private:
    TwoWire& _wire;
    bool _initialized;

    float _co2;
    float _temperature;
    float _humidity;

    bool _sendCommand(uint16_t cmd);
    bool _sendCommand(uint16_t cmd, uint16_t arg);
    bool _readData(uint8_t* data, uint8_t len);
    uint8_t _computeCRC8(uint8_t* data, uint8_t len);
};

// =============================================================================
// LTC4015 - Battery Charger and Power Management
// =============================================================================
class LTC4015 {
public:
    LTC4015(TwoWire& wire = Wire);

    bool begin();
    bool isConnected();

    // Voltage readings (in Volts)
    float readBatteryVoltage();
    float readInputVoltage();
    float readSystemVoltage();

    // Current readings (in Amps)
    float readBatteryChargeCurrent();
    float readInputCurrent();

    // Temperature
    float readDieTemperature();   // Die temperature in Celsius

    // Status
    uint16_t getChargerState();
    uint16_t getChargeStatus();
    uint16_t getSystemStatus();

    // Battery configuration
    uint8_t getCellCount();
    uint8_t getChemistry();

    // Charger control
    void suspendCharging();
    void resumeCharging();
    void enableMPPT(bool enable);
    void enableForceTelemetry(bool enable);
    void enableCoulombCounter(bool enable);

    // Status string helpers
    const char* getChargerStateString();
    const char* getChemistryString();

private:
    TwoWire& _wire;
    bool _initialized;

    uint16_t _readRegister16(uint8_t reg);
    void _writeRegister16(uint8_t reg, uint16_t value);

    // Conversion constants (from LTC4015 datasheet)
    static constexpr float VBAT_SCALE = 0.000192264f;    // V per LSB
    static constexpr float VIN_SCALE = 0.001648f;         // V per LSB
    static constexpr float VSYS_SCALE = 0.001648f;        // V per LSB
    static constexpr float IBAT_SCALE = 0.00146484f;      // A per LSB (with 10mOhm sense)
    static constexpr float IIN_SCALE = 0.00146484f;       // A per LSB
    static constexpr float TEMP_SCALE = 0.0215f;          // C per LSB
};

#endif // WALTER_FEELS_SENSORS_H
