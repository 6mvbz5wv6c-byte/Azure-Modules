/**
 * @file walter_feels.cpp
 * @brief Walter Feels Hardware Abstraction Layer Implementation
 */

#include "walter_feels.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Static member initialization
SerialMode_t WalterFeels::_currentSerialMode = SERIAL_MODE_OFF;
bool WalterFeels::_gpioAState = false;
bool WalterFeels::_gpioBState = false;
bool WalterFeels::_initialized = false;

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

    // Power up sequence
    setI2cBusPower(true);
    delay(10);
    set3v3(true);
    delay(100);  // Allow power to stabilize

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
    // Configure LEDC for PWM soft-start on 3.3V rail
    ledc_timer_config_t timerConfig = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_5_BIT,  // 0-31
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timerConfig);

    ledc_channel_config_t channelConfig = {
        .gpio_num = WFEELS_PIN_3V3_EN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 0 }
    };
    ledc_channel_config(&channelConfig);
}

void WalterFeels::_softStart3v3() {
    // Soft-start ramp from 0 to full duty over 100ms
    for (int duty = 0; duty <= 31; duty++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        delay(3);
    }
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
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
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
