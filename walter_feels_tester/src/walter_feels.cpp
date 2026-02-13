/**
 * @file walter_feels.cpp
 * @brief Walter Feels Hardware Abstraction Layer Implementation
 */

#include "walter_feels.h"
#include "driver/gpio.h"

// Static member initialization
SerialMode_t WalterFeels::_currentSerialMode = SERIAL_MODE_OFF;
bool WalterFeels::_gpioAState = false;
bool WalterFeels::_gpioBState = false;
bool WalterFeels::_initialized = false;

// LEDC configuration for 3.3V soft-start
static const uint8_t LEDC_CHANNEL = 0;
static const uint32_t LEDC_FREQ = 1000;
static const uint8_t LEDC_RESOLUTION = 5;  // 5-bit = 0-31

// Secondary I2C bus for CO2 sensor
TwoWire Wire1 = TwoWire(1);

bool WalterFeels::init() {
    if (_initialized) {
        return true;
    }

    Serial.println("[WalterFeels] Initializing hardware...");

    _initPins();
    _initLedc();

    // Initialize I2C buses
    Wire.begin(WFEELS_PIN_I2C_SDA, WFEELS_PIN_I2C_SCL, WFEELS_I2C_FREQ);
    Wire1.begin(WFEELS_PIN_CO2_SDA, WFEELS_PIN_CO2_SCL, WFEELS_I2C_FREQ);

    // Power up sequence: 3.3V rail first, then I2C bus power
    set3v3(true);
    delay(250);  // Allow 3.3V rail to stabilize
    setI2cBusPower(true);
    delay(100);  // Allow I2C bus to stabilize

    _initialized = true;
    Serial.println("[WalterFeels] Initialization complete");

    return true;
}

void WalterFeels::_initPins() {
    // Disable all hold states from previous deep sleep
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_3V3_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_12V_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_I2C_BUS_PWR);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_CO2_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_CAN_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_RS232_TX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_RS232_RX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_RS485_TX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_RS485_RX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_SDI12_TX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_SDI12_RX_EN);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_GPIO_A);
    gpio_hold_dis((gpio_num_t)WFEELS_PIN_GPIO_B);

    // Configure power control pins as outputs (start disabled)
    pinMode(WFEELS_PIN_12V_EN, OUTPUT);
    pinMode(WFEELS_PIN_I2C_BUS_PWR, OUTPUT);
    pinMode(WFEELS_PIN_CO2_EN, OUTPUT);
    pinMode(WFEELS_PIN_CAN_EN, OUTPUT);

    digitalWrite(WFEELS_PIN_12V_EN, LOW);
    digitalWrite(WFEELS_PIN_I2C_BUS_PWR, LOW);
    digitalWrite(WFEELS_PIN_CO2_EN, LOW);
    digitalWrite(WFEELS_PIN_CAN_EN, LOW);

    // Configure serial enable pins (active low typically)
    pinMode(WFEELS_PIN_RS232_TX_EN, OUTPUT);
    pinMode(WFEELS_PIN_RS232_RX_EN, OUTPUT);
    pinMode(WFEELS_PIN_RS485_TX_EN, OUTPUT);
    pinMode(WFEELS_PIN_RS485_RX_EN, OUTPUT);
    pinMode(WFEELS_PIN_SDI12_TX_EN, OUTPUT);
    pinMode(WFEELS_PIN_SDI12_RX_EN, OUTPUT);

    // All serial interfaces off initially
    digitalWrite(WFEELS_PIN_RS232_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS232_RX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS485_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS485_RX_EN, LOW);
    digitalWrite(WFEELS_PIN_SDI12_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_SDI12_RX_EN, LOW);

    // Configure GPIO A and B as outputs
    pinMode(WFEELS_PIN_GPIO_A, OUTPUT);
    pinMode(WFEELS_PIN_GPIO_B, OUTPUT);
    digitalWrite(WFEELS_PIN_GPIO_A, LOW);
    digitalWrite(WFEELS_PIN_GPIO_B, LOW);

    // CAN pins with pullup
    pinMode(WFEELS_PIN_CAN_RX, INPUT_PULLUP);
    pinMode(WFEELS_PIN_CAN_TX, OUTPUT);

    // LTC4015 alert pin
    pinMode(WFEELS_PIN_LTC4015_SMBALERT, INPUT);
    pinMode(WFEELS_PIN_LTC4015_MPPT, OUTPUT);
}

void WalterFeels::_initLedc() {
    // Configure LEDC for PWM soft-start on 3.3V rail using Arduino API
    ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
    ledcAttachPin(WFEELS_PIN_3V3_EN, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 0);  // Start with output disabled
}

void WalterFeels::_softStart3v3() {
    // Soft-start ramp from 0 to max duty over ~100ms
    for (int duty = 0; duty <= 31; duty++) {
        ledcWrite(LEDC_CHANNEL, duty);
        delay(3);
    }

    // LEDC max duty (31/32) is only 96.8%, not a solid HIGH.
    // Detach from LEDC and switch to plain GPIO so the enable pin
    // is driven fully HIGH for a stable 3.3V rail.
    ledcDetachPin(WFEELS_PIN_3V3_EN);
    pinMode(WFEELS_PIN_3V3_EN, OUTPUT);
    digitalWrite(WFEELS_PIN_3V3_EN, HIGH);
}

void WalterFeels::prepareDeepSleep() {
    // Disable all peripherals
    set3v3(false);
    set12v(false);
    setI2cBusPower(false);
    setCo2Power(false);
    setCanPower(false);
    setSerialMode(SERIAL_MODE_OFF);

    // Enable hold on all pins to maintain state during deep sleep
    gpio_hold_en((gpio_num_t)WFEELS_PIN_3V3_EN);
    gpio_hold_en((gpio_num_t)WFEELS_PIN_12V_EN);
    gpio_hold_en((gpio_num_t)WFEELS_PIN_I2C_BUS_PWR);
    gpio_hold_en((gpio_num_t)WFEELS_PIN_CO2_EN);
    gpio_hold_en((gpio_num_t)WFEELS_PIN_CAN_EN);

    gpio_deep_sleep_hold_en();
}

void WalterFeels::set3v3(bool enable) {
    if (enable) {
        _softStart3v3();
    } else {
        // Ensure pin is plain GPIO (may have been left as LEDC)
        ledcDetachPin(WFEELS_PIN_3V3_EN);
        pinMode(WFEELS_PIN_3V3_EN, OUTPUT);
        digitalWrite(WFEELS_PIN_3V3_EN, LOW);
    }
}

void WalterFeels::set12v(bool enable) {
    digitalWrite(WFEELS_PIN_12V_EN, enable ? HIGH : LOW);
}

void WalterFeels::setI2cBusPower(bool enable) {
    digitalWrite(WFEELS_PIN_I2C_BUS_PWR, enable ? HIGH : LOW);
}

void WalterFeels::setCo2Power(bool enable) {
    digitalWrite(WFEELS_PIN_CO2_EN, enable ? HIGH : LOW);
}

void WalterFeels::setCanPower(bool enable) {
    digitalWrite(WFEELS_PIN_CAN_EN, enable ? HIGH : LOW);
}

void WalterFeels::setSerialMode(SerialMode_t mode) {
    // First disable all transceivers
    digitalWrite(WFEELS_PIN_RS232_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS232_RX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS485_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_RS485_RX_EN, LOW);
    digitalWrite(WFEELS_PIN_SDI12_TX_EN, LOW);
    digitalWrite(WFEELS_PIN_SDI12_RX_EN, LOW);

    switch (mode) {
        case SERIAL_MODE_RS232:
            set3v3(true);
            digitalWrite(WFEELS_PIN_RS232_TX_EN, HIGH);
            digitalWrite(WFEELS_PIN_RS232_RX_EN, HIGH);
            break;

        case SERIAL_MODE_RS485_TX:
            set3v3(true);
            digitalWrite(WFEELS_PIN_RS485_TX_EN, HIGH);
            break;

        case SERIAL_MODE_RS485_RX:
            set3v3(true);
            digitalWrite(WFEELS_PIN_RS485_RX_EN, HIGH);
            break;

        case SERIAL_MODE_SDI12_TX:
            set3v3(true);
            digitalWrite(WFEELS_PIN_SDI12_TX_EN, HIGH);
            break;

        case SERIAL_MODE_SDI12_RX:
            set3v3(true);
            digitalWrite(WFEELS_PIN_SDI12_RX_EN, HIGH);
            break;

        case SERIAL_MODE_OFF:
        default:
            break;
    }

    _currentSerialMode = mode;
}

SerialMode_t WalterFeels::getSerialMode() {
    return _currentSerialMode;
}

void WalterFeels::setGpioA(bool state) {
    _gpioAState = state;
    digitalWrite(WFEELS_PIN_GPIO_A, state ? HIGH : LOW);
}

void WalterFeels::setGpioB(bool state) {
    _gpioBState = state;
    digitalWrite(WFEELS_PIN_GPIO_B, state ? HIGH : LOW);
}

bool WalterFeels::getGpioA() {
    return _gpioAState;
}

bool WalterFeels::getGpioB() {
    return _gpioBState;
}

void WalterFeels::toggleGpioA() {
    setGpioA(!_gpioAState);
}

void WalterFeels::toggleGpioB() {
    setGpioB(!_gpioBState);
}

TwoWire& WalterFeels::getI2cBus() {
    return Wire;
}

TwoWire& WalterFeels::getCo2I2cBus() {
    return Wire1;
}

bool WalterFeels::scanI2cDevice(uint8_t address) {
    return scanI2cDevice(Wire, address);
}

bool WalterFeels::scanI2cDevice(TwoWire& wire, uint8_t address) {
    wire.beginTransmission(address);
    return (wire.endTransmission() == 0);
}

void WalterFeels::scanAllI2c() {
    Serial.println("\n[I2C Scan] Primary Bus (Wire):");
    int count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (scanI2cDevice(Wire, addr)) {
            Serial.printf("  Found device at 0x%02X\n", addr);
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  No devices found");
    }

    Serial.println("[I2C Scan] Secondary Bus (Wire1 - CO2):");
    count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (scanI2cDevice(Wire1, addr)) {
            Serial.printf("  Found device at 0x%02X\n", addr);
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  No devices found");
    }
}
