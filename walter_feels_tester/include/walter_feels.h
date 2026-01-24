/**
 * @file walter_feels.h
 * @brief Walter Feels Hardware Abstraction Layer
 *
 * Provides low-level control of the Walter Feels carrier board including
 * power management, GPIO control, and serial interface multiplexing.
 */

#ifndef WALTER_FEELS_H
#define WALTER_FEELS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

class WalterFeels {
public:
    /**
     * @brief Initialize all GPIO pins and power rails
     * @return true if initialization successful
     */
    static bool init();

    /**
     * @brief Prepare for deep sleep (hold GPIO states)
     */
    static void prepareDeepSleep();

    // Power Management
    static void set3v3(bool enable);
    static void set12v(bool enable);
    static void setI2cBusPower(bool enable);
    static void setCo2Power(bool enable);
    static void setCanPower(bool enable);

    // Serial Mode Control
    static void setSerialMode(SerialMode_t mode);
    static SerialMode_t getSerialMode();

    // GPIO Control
    static void setGpioA(bool state);
    static void setGpioB(bool state);
    static bool getGpioA();
    static bool getGpioB();
    static void toggleGpioA();
    static void toggleGpioB();

    // I2C Bus Access
    static TwoWire& getI2cBus();      // Primary I2C (sensors, OLED)
    static TwoWire& getCo2I2cBus();   // Secondary I2C (CO2 sensor)

    // Utility
    static bool scanI2cDevice(uint8_t address);
    static bool scanI2cDevice(TwoWire& wire, uint8_t address);
    static void scanAllI2c();

private:
    static void _initPins();
    static void _initLedc();
    static void _softStart3v3();

    static SerialMode_t _currentSerialMode;
    static bool _gpioAState;
    static bool _gpioBState;
    static bool _initialized;
};

#endif // WALTER_FEELS_H
