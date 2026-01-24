/**
 * @file main.cpp
 * @brief Walter Feels Hardware Tester - Main Entry Point
 *
 * Comprehensive hardware test program for the Walter Feels expansion board.
 * Tests all onboard features and reports pass/fail results via Serial and OLED.
 *
 * Features tested:
 * - I2C bus scan (primary and secondary)
 * - SSD1306 OLED display
 * - HDC1080 temperature/humidity sensor
 * - LPS22HB barometric pressure sensor
 * - SCD30 CO2 sensor
 * - LTC4015 battery charger/power management
 * - Power rails (3.3V, 12V, I2C bus power)
 * - GPIO A and B with different toggle frequencies
 * - Serial interfaces (RS232, RS485, SDI-12)
 * - SD card pins
 * - CAN bus pins
 *
 * GPIO Toggle:
 * - GPIO A: 1 Hz (toggles every 500ms)
 * - GPIO B: 5 Hz (toggles every 100ms)
 *
 * Author: Generated for Walter Feels Hardware Test
 * License: GPLv3 (matching Walter hardware license)
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "walter_feels.h"
#include "sensors.h"
#include "ssd1306.h"
#include "test_suite.h"

// =============================================================================
// Global Objects
// =============================================================================

// Display
SSD1306 display(Wire, SSD1306_I2C_ADDR);

// Sensors (on primary I2C)
HDC1080 hdc1080(Wire);
LPS22HB lps22hb(Wire);
LTC4015 ltc4015(Wire);

// CO2 sensor (on secondary I2C)
extern TwoWire Wire1;
SCD30 scd30(Wire1);

// Test suite
TestSuite* testSuite = nullptr;

// GPIO toggle timing
volatile uint32_t lastToggleA = 0;
volatile uint32_t lastToggleB = 0;
const uint32_t TOGGLE_INTERVAL_A = 500;  // 1 Hz (500ms half-period)
const uint32_t TOGGLE_INTERVAL_B = 100;  // 5 Hz (100ms half-period)

// Test mode flag
bool testComplete = false;
bool continuousMode = false;

// =============================================================================
// Function Prototypes
// =============================================================================

void printBanner();
void initializeHardware();
void registerTests();
void runGpioToggle();
void showContinuousStatus();
void handleSerialCommands();

// =============================================================================
// Setup
// =============================================================================

void setup() {
    // Initialize serial first for debug output
    Serial.begin(921600);
    delay(1000);  // Allow USB CDC to enumerate

    printBanner();

    // Initialize Walter Feels hardware
    Serial.println("[Setup] Initializing Walter Feels hardware...");
    if (!WalterFeels::init()) {
        Serial.println("[Setup] ERROR: Failed to initialize Walter Feels!");
    }

    // Wait for power to stabilize
    delay(500);

    // Scan I2C buses
    Serial.println("\n[Setup] Scanning I2C buses...");
    WalterFeels::scanAllI2c();

    // Initialize OLED display
    Serial.println("\n[Setup] Initializing OLED display...");
    if (display.begin()) {
        Serial.println("[Setup] OLED display initialized");
        display.showStatus("WALTER FEELS", "HW Tester v" TESTER_VERSION, "", "Initializing...");
    } else {
        Serial.println("[Setup] WARNING: OLED display not found!");
    }

    // Set global pointers for test functions
    g_display = &display;
    g_hdc1080 = &hdc1080;
    g_lps22hb = &lps22hb;
    g_scd30 = &scd30;
    g_ltc4015 = &ltc4015;

    // Create test suite
    testSuite = new TestSuite(display);
    registerTests();

    delay(1000);

    // Run all tests
    Serial.println("\n[Setup] Starting hardware tests...\n");
    testSuite->runAll();

    // Print results
    testSuite->printSummary();
    testSuite->printDetailedReport();
    testSuite->displaySummary();

    testComplete = true;

    // Show final result on display with pass/fail indication
    if (testSuite->allPassed()) {
        Serial.println("\n*** ALL TESTS PASSED ***\n");
    } else {
        Serial.printf("\n*** %d TEST(S) FAILED ***\n\n", testSuite->getFailCount());
    }

    Serial.println("Entering continuous monitoring mode...");
    Serial.println("Commands: 'r' = rerun tests, 's' = sensor readings, 'p' = power status");
    Serial.println("GPIO A toggles at 1 Hz, GPIO B at 5 Hz\n");

    continuousMode = true;
}

// =============================================================================
// Main Loop
// =============================================================================

void loop() {
    // Handle GPIO toggling at different frequencies
    runGpioToggle();

    // Handle serial commands
    handleSerialCommands();

    // Update display periodically in continuous mode
    static uint32_t lastDisplayUpdate = 0;
    if (continuousMode && millis() - lastDisplayUpdate > 2000) {
        showContinuousStatus();
        lastDisplayUpdate = millis();
    }

    // Small delay to prevent tight loop
    delay(10);
}

// =============================================================================
// Implementation
// =============================================================================

void printBanner() {
    Serial.println("\n");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║            WALTER FEELS HARDWARE TESTER                    ║");
    Serial.println("║                    Version " TESTER_VERSION "                            ║");
    Serial.println("╠════════════════════════════════════════════════════════════╣");
    Serial.println("║  Platform: ESP32-S3 + Sequans Walter                       ║");
    Serial.println("║  Board: Walter Feels Expansion                             ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println();
}

void initializeHardware() {
    // Hardware initialization is done in WalterFeels::init()
}

void registerTests() {
    // Register all hardware tests
    testSuite->registerTest("I2C Bus Scan", "Scan both I2C buses",
                           TestSuite::testI2cBusScan);

    testSuite->registerTest("OLED Display", "Test SSD1306 display",
                           TestSuite::testOledDisplay);

    testSuite->registerTest("HDC1080", "Temp/humidity sensor",
                           TestSuite::testHdc1080);

    testSuite->registerTest("LPS22HB", "Pressure sensor",
                           TestSuite::testLps22hb);

    testSuite->registerTest("SCD30", "CO2 sensor",
                           TestSuite::testScd30);

    testSuite->registerTest("LTC4015", "Battery charger",
                           TestSuite::testLtc4015);

    testSuite->registerTest("Power Rails", "3.3V and I2C power",
                           TestSuite::testPowerRails);

    testSuite->registerTest("GPIO Output", "GPIO A and B pins",
                           TestSuite::testGpioOutput);

    testSuite->registerTest("Serial Modes", "RS232/RS485/SDI12",
                           TestSuite::testSerialModes);

    testSuite->registerTest("SD Card Pins", "SD interface pins",
                           TestSuite::testSdCardPins);

    testSuite->registerTest("CAN Bus Pins", "CAN interface",
                           TestSuite::testCanPins);

    testSuite->registerTest("12V Rail", "12V power enable",
                           TestSuite::test12vRail);
}

void runGpioToggle() {
    uint32_t now = millis();

    // Toggle GPIO A at 1 Hz
    if (now - lastToggleA >= TOGGLE_INTERVAL_A) {
        WalterFeels::toggleGpioA();
        lastToggleA = now;
    }

    // Toggle GPIO B at 5 Hz
    if (now - lastToggleB >= TOGGLE_INTERVAL_B) {
        WalterFeels::toggleGpioB();
        lastToggleB = now;
    }
}

void showContinuousStatus() {
    static uint8_t displayPage = 0;

    char line1[22], line2[22], line3[22], line4[22];

    switch (displayPage) {
        case 0:
            // Show test results
            snprintf(line1, sizeof(line1), "Test: %s",
                    testSuite->allPassed() ? "PASS" : "FAIL");
            snprintf(line2, sizeof(line2), "P:%d F:%d S:%d",
                    testSuite->getPassCount(),
                    testSuite->getFailCount(),
                    testSuite->getSkipCount());
            snprintf(line3, sizeof(line3), "GPIO A:%d B:%d",
                    WalterFeels::getGpioA() ? 1 : 0,
                    WalterFeels::getGpioB() ? 1 : 0);
            snprintf(line4, sizeof(line4), "Uptime: %lus", millis() / 1000);
            break;

        case 1:
            // Show sensor readings
            {
                float temp = hdc1080.readTemperature();
                float hum = hdc1080.readHumidity();
                float press = lps22hb.readPressure();

                snprintf(line1, sizeof(line1), "Temp: %.1f C", temp);
                snprintf(line2, sizeof(line2), "Humidity: %.1f%%", hum);
                snprintf(line3, sizeof(line3), "Press: %.1f hPa", press);
                snprintf(line4, sizeof(line4), "");
            }
            break;

        case 2:
            // Show power status
            if (ltc4015.isConnected()) {
                float vBat = ltc4015.readBatteryVoltage();
                float vIn = ltc4015.readInputVoltage();
                float vSys = ltc4015.readSystemVoltage();

                snprintf(line1, sizeof(line1), "Vbat: %.2f V", vBat);
                snprintf(line2, sizeof(line2), "Vin:  %.2f V", vIn);
                snprintf(line3, sizeof(line3), "Vsys: %.2f V", vSys);
                snprintf(line4, sizeof(line4), "%s", ltc4015.getChargerStateString());
            } else {
                snprintf(line1, sizeof(line1), "LTC4015 not found");
                line2[0] = line3[0] = line4[0] = '\0';
            }
            break;
    }

    display.showStatus(line1, line2, line3, line4);

    displayPage = (displayPage + 1) % 3;
}

void handleSerialCommands() {
    if (Serial.available()) {
        char cmd = Serial.read();

        switch (cmd) {
            case 'r':
            case 'R':
                Serial.println("\n[Command] Re-running all tests...\n");
                testSuite->runAll();
                testSuite->printSummary();
                testSuite->printDetailedReport();
                testSuite->displaySummary();
                break;

            case 's':
            case 'S':
                Serial.println("\n[Command] Sensor readings:");
                {
                    float temp, hum;
                    hdc1080.readBoth(temp, hum);
                    Serial.printf("  HDC1080: %.2f C, %.2f%% RH\n", temp, hum);

                    float press = lps22hb.readPressure();
                    float pTemp = lps22hb.readTemperature();
                    Serial.printf("  LPS22HB: %.2f hPa, %.2f C\n", press, pTemp);

                    if (scd30.dataAvailable()) {
                        scd30.readMeasurement();
                        Serial.printf("  SCD30: %.0f ppm CO2, %.2f C, %.2f%% RH\n",
                                      scd30.getCO2(), scd30.getTemperature(),
                                      scd30.getHumidity());
                    } else {
                        Serial.println("  SCD30: No data available");
                    }
                }
                Serial.println();
                break;

            case 'p':
            case 'P':
                Serial.println("\n[Command] Power status:");
                if (ltc4015.isConnected()) {
                    Serial.printf("  Vbat: %.3f V\n", ltc4015.readBatteryVoltage());
                    Serial.printf("  Vin:  %.3f V\n", ltc4015.readInputVoltage());
                    Serial.printf("  Vsys: %.3f V\n", ltc4015.readSystemVoltage());
                    Serial.printf("  Ibat: %.3f A\n", ltc4015.readBatteryChargeCurrent());
                    Serial.printf("  Iin:  %.3f A\n", ltc4015.readInputCurrent());
                    Serial.printf("  Die temp: %.1f C\n", ltc4015.readDieTemperature());
                    Serial.printf("  State: %s\n", ltc4015.getChargerStateString());
                    Serial.printf("  Chemistry: %s, %d cells\n",
                                  ltc4015.getChemistryString(),
                                  ltc4015.getCellCount());
                } else {
                    Serial.println("  LTC4015 not connected");
                }
                Serial.println();
                break;

            case 'i':
            case 'I':
                Serial.println("\n[Command] I2C scan:");
                WalterFeels::scanAllI2c();
                Serial.println();
                break;

            case 'g':
            case 'G':
                Serial.println("\n[Command] GPIO status:");
                Serial.printf("  GPIO A (pin %d): %s\n", WFEELS_PIN_GPIO_A,
                             WalterFeels::getGpioA() ? "HIGH" : "LOW");
                Serial.printf("  GPIO B (pin %d): %s\n", WFEELS_PIN_GPIO_B,
                             WalterFeels::getGpioB() ? "HIGH" : "LOW");
                Serial.println();
                break;

            case 'h':
            case 'H':
            case '?':
                Serial.println("\n[Help] Available commands:");
                Serial.println("  r - Re-run all hardware tests");
                Serial.println("  s - Read all sensors");
                Serial.println("  p - Power/battery status");
                Serial.println("  i - I2C bus scan");
                Serial.println("  g - GPIO status");
                Serial.println("  h - This help message");
                Serial.println();
                break;
        }
    }
}
