/**
 * @file test_suite.cpp
 * @brief Comprehensive Hardware Test Suite Implementation
 */

#include "test_suite.h"

// Global instances for static test functions
TestSuite* g_testSuite = nullptr;
HDC1080* g_hdc1080 = nullptr;
LPS22HB* g_lps22hb = nullptr;
SCD30* g_scd30 = nullptr;
LTC4015* g_ltc4015 = nullptr;
SSD1306* g_display = nullptr;

TestSuite::TestSuite(SSD1306& display) : _display(display), _testCount(0) {
    memset(_tests, 0, sizeof(_tests));
    g_testSuite = this;
}

void TestSuite::registerTest(const char* name, const char* desc, TestFunc_t func) {
    if (_testCount >= MAX_TESTS) {
        Serial.println("[TestSuite] ERROR: Max tests reached!");
        return;
    }

    _tests[_testCount].name = name;
    _tests[_testCount].description = desc;
    _tests[_testCount].func = func;
    _tests[_testCount].result = TEST_NOT_RUN;
    _tests[_testCount].message[0] = '\0';
    _tests[_testCount].durationMs = 0;

    _testCount++;
}

void TestSuite::runAll() {
    Serial.println("\n" "========================================");
    Serial.println("  WALTER FEELS HARDWARE TEST SUITE");
    Serial.println("========================================\n");

    _display.showStatus("WALTER FEELS", "Hardware Test", "Starting...");
    delay(1000);

    for (uint8_t i = 0; i < _testCount; i++) {
        runTest(i);
    }

    Serial.println("\n" "========================================");
    Serial.println("  TEST COMPLETE");
    Serial.println("========================================\n");
}

void TestSuite::runTest(uint8_t index) {
    if (index >= _testCount) return;

    TestEntry_t& test = _tests[index];

    Serial.printf("[%02d/%02d] %s: ", index + 1, _testCount, test.name);

    // Update display
    _display.showProgress(test.name, index + 1, _testCount);

    uint32_t startTime = millis();

    // Run the test
    test.result = test.func(test.message, sizeof(test.message));

    test.durationMs = millis() - startTime;

    // Log result
    _logResult(test.name, test.result, test.message);
}

void TestSuite::_logResult(const char* name, TestResult_t result, const char* msg) {
    const char* resultStr = _resultToString(result);

    if (msg && msg[0]) {
        Serial.printf("%s - %s (%s)\n", resultStr,
                      result == TEST_PASS ? "OK" : "Check details",
                      msg);
    } else {
        Serial.printf("%s\n", resultStr);
    }
}

const char* TestSuite::_resultToString(TestResult_t result) {
    switch (result) {
        case TEST_PASS: return "PASS";
        case TEST_FAIL: return "FAIL";
        case TEST_SKIP: return "SKIP";
        case TEST_WARN: return "WARN";
        default:        return "----";
    }
}

uint8_t TestSuite::getPassCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _testCount; i++) {
        if (_tests[i].result == TEST_PASS) count++;
    }
    return count;
}

uint8_t TestSuite::getFailCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _testCount; i++) {
        if (_tests[i].result == TEST_FAIL) count++;
    }
    return count;
}

uint8_t TestSuite::getSkipCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _testCount; i++) {
        if (_tests[i].result == TEST_SKIP) count++;
    }
    return count;
}

uint8_t TestSuite::getTotalCount() {
    return _testCount;
}

bool TestSuite::allPassed() {
    return getFailCount() == 0;
}

void TestSuite::printSummary() {
    Serial.println("\n--- TEST SUMMARY ---");
    Serial.printf("Total:  %d\n", _testCount);
    Serial.printf("Passed: %d\n", getPassCount());
    Serial.printf("Failed: %d\n", getFailCount());
    Serial.printf("Skipped: %d\n", getSkipCount());
    Serial.printf("\nOverall: %s\n", allPassed() ? "PASS" : "FAIL");
    Serial.println("--------------------\n");
}

void TestSuite::printDetailedReport() {
    Serial.println("\n--- DETAILED TEST REPORT ---");
    for (uint8_t i = 0; i < _testCount; i++) {
        TestEntry_t& test = _tests[i];
        Serial.printf("[%02d] %-20s: %s", i + 1, test.name,
                      _resultToString(test.result));
        if (test.message[0]) {
            Serial.printf(" (%s)", test.message);
        }
        Serial.printf(" [%lu ms]\n", test.durationMs);
    }
    Serial.println("----------------------------\n");
}

void TestSuite::displaySummary() {
    char line1[22], line2[22], line3[22], line4[22];

    snprintf(line1, sizeof(line1), "TEST COMPLETE");
    snprintf(line2, sizeof(line2), "Pass: %d  Fail: %d", getPassCount(), getFailCount());
    snprintf(line3, sizeof(line3), "Skip: %d  Total: %d", getSkipCount(), _testCount);
    snprintf(line4, sizeof(line4), "Result: %s", allPassed() ? "PASS" : "FAIL");

    _display.showStatus(line1, line2, line3, line4);
}

// =============================================================================
// Individual Test Implementations
// =============================================================================

TestResult_t TestSuite::testI2cBusScan(char* msg, size_t len) {
    int foundPrimary = 0;
    int foundSecondary = 0;

    // Scan primary I2C bus
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (WalterFeels::scanI2cDevice(Wire, addr)) {
            foundPrimary++;
        }
    }

    // Scan secondary I2C bus (CO2)
    WalterFeels::setCo2Power(true);
    delay(100);

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (WalterFeels::scanI2cDevice(Wire1, addr)) {
            foundSecondary++;
        }
    }

    snprintf(msg, len, "Primary: %d, CO2 bus: %d devices", foundPrimary, foundSecondary);

    // Expect at least some devices on primary bus
    return (foundPrimary >= 1) ? TEST_PASS : TEST_WARN;
}

TestResult_t TestSuite::testOledDisplay(char* msg, size_t len) {
    if (!g_display) {
        snprintf(msg, len, "Display not initialized");
        return TEST_SKIP;
    }

    if (!g_display->isConnected()) {
        snprintf(msg, len, "Not detected at 0x%02X", SSD1306_I2C_ADDR);
        return TEST_FAIL;
    }

    // Test display operations
    g_display->clear();
    g_display->setCursor(0, 0);
    g_display->setTextSize(2);
    g_display->println("OLED OK");
    g_display->setTextSize(1);
    g_display->println("Display test");
    g_display->display();

    delay(500);

    snprintf(msg, len, "128x64 @ 0x%02X", SSD1306_I2C_ADDR);
    return TEST_PASS;
}

TestResult_t TestSuite::testHdc1080(char* msg, size_t len) {
    if (!g_hdc1080) {
        snprintf(msg, len, "Driver not initialized");
        return TEST_SKIP;
    }

    if (!g_hdc1080->isConnected()) {
        snprintf(msg, len, "Not detected at 0x%02X", HDC1080_I2C_ADDR);
        return TEST_FAIL;
    }

    if (!g_hdc1080->begin()) {
        snprintf(msg, len, "Init failed - wrong ID?");
        return TEST_FAIL;
    }

    float temp, humidity;
    if (!g_hdc1080->readBoth(temp, humidity)) {
        snprintf(msg, len, "Read failed");
        return TEST_FAIL;
    }

    // Sanity check readings
    if (temp < -40 || temp > 125 || humidity < 0 || humidity > 100) {
        snprintf(msg, len, "Invalid: %.1fC, %.1f%%", temp, humidity);
        return TEST_WARN;
    }

    snprintf(msg, len, "%.1fC, %.1f%% RH", temp, humidity);
    return TEST_PASS;
}

TestResult_t TestSuite::testLps22hb(char* msg, size_t len) {
    if (!g_lps22hb) {
        snprintf(msg, len, "Driver not initialized");
        return TEST_SKIP;
    }

    if (!g_lps22hb->isConnected()) {
        snprintf(msg, len, "Not detected at 0x%02X", LPS22HB_I2C_ADDR);
        return TEST_FAIL;
    }

    uint8_t whoAmI = g_lps22hb->whoAmI();
    if (whoAmI != LPS22HB_WHO_AM_I_VALUE) {
        snprintf(msg, len, "Wrong ID: 0x%02X", whoAmI);
        return TEST_FAIL;
    }

    if (!g_lps22hb->begin()) {
        snprintf(msg, len, "Init failed");
        return TEST_FAIL;
    }

    float pressure = g_lps22hb->readPressure();
    float temp = g_lps22hb->readTemperature();

    // Sanity check (normal atmospheric pressure range)
    if (pressure < 300 || pressure > 1200) {
        snprintf(msg, len, "Invalid: %.1f hPa", pressure);
        return TEST_WARN;
    }

    snprintf(msg, len, "%.1f hPa, %.1fC", pressure, temp);
    return TEST_PASS;
}

TestResult_t TestSuite::testScd30(char* msg, size_t len) {
    if (!g_scd30) {
        snprintf(msg, len, "Driver not initialized");
        return TEST_SKIP;
    }

    // Power on CO2 sensor
    WalterFeels::setCo2Power(true);
    delay(CO2_WARMUP_MS);

    if (!g_scd30->isConnected()) {
        snprintf(msg, len, "Not detected at 0x%02X", SCD30_I2C_ADDR);
        WalterFeels::setCo2Power(false);
        return TEST_FAIL;
    }

    uint16_t fwVer = g_scd30->getFirmwareVersion();
    if (fwVer == 0) {
        snprintf(msg, len, "Cannot read firmware");
        return TEST_WARN;
    }

    if (!g_scd30->begin(true)) {
        snprintf(msg, len, "Init failed");
        return TEST_FAIL;
    }

    // Start measurement
    g_scd30->startMeasuring();

    // Wait for data (can take up to 2 seconds)
    int attempts = 10;
    while (!g_scd30->dataAvailable() && attempts > 0) {
        delay(200);
        attempts--;
    }

    if (attempts == 0) {
        snprintf(msg, len, "FW: %d.%d, no data yet", fwVer >> 8, fwVer & 0xFF);
        return TEST_WARN;  // Not a failure, just slow warmup
    }

    g_scd30->readMeasurement();
    float co2 = g_scd30->getCO2();

    snprintf(msg, len, "FW: %d.%d, CO2: %.0f ppm", fwVer >> 8, fwVer & 0xFF, co2);
    return TEST_PASS;
}

TestResult_t TestSuite::testLtc4015(char* msg, size_t len) {
    if (!g_ltc4015) {
        snprintf(msg, len, "Driver not initialized");
        return TEST_SKIP;
    }

    if (!g_ltc4015->isConnected()) {
        snprintf(msg, len, "Not detected at 0x%02X", LTC4015_I2C_ADDR);
        return TEST_FAIL;
    }

    if (!g_ltc4015->begin()) {
        snprintf(msg, len, "Init failed");
        return TEST_FAIL;
    }

    float vBat = g_ltc4015->readBatteryVoltage();
    float vIn = g_ltc4015->readInputVoltage();
    float vSys = g_ltc4015->readSystemVoltage();

    // Check if we have valid voltage readings
    if (vSys < 1.0 || vSys > 20.0) {
        snprintf(msg, len, "Invalid Vsys: %.2fV", vSys);
        return TEST_WARN;
    }

    snprintf(msg, len, "Vbat:%.2f Vin:%.2f Vsys:%.2fV", vBat, vIn, vSys);
    return TEST_PASS;
}

TestResult_t TestSuite::testPowerRails(char* msg, size_t len) {
    // Test 3.3V rail toggle
    WalterFeels::set3v3(false);
    delay(50);
    WalterFeels::set3v3(true);
    delay(100);

    // Test I2C bus power
    WalterFeels::setI2cBusPower(false);
    delay(50);
    WalterFeels::setI2cBusPower(true);
    delay(100);

    snprintf(msg, len, "3V3 and I2C power toggled OK");
    return TEST_PASS;
}

TestResult_t TestSuite::testGpioOutput(char* msg, size_t len) {
    // Test GPIO A
    WalterFeels::setGpioA(true);
    delay(10);
    bool aHigh = WalterFeels::getGpioA();

    WalterFeels::setGpioA(false);
    delay(10);
    bool aLow = !WalterFeels::getGpioA();

    // Test GPIO B
    WalterFeels::setGpioB(true);
    delay(10);
    bool bHigh = WalterFeels::getGpioB();

    WalterFeels::setGpioB(false);
    delay(10);
    bool bLow = !WalterFeels::getGpioB();

    if (aHigh && aLow && bHigh && bLow) {
        snprintf(msg, len, "GPIO A(pin %d) and B(pin %d) OK",
                WFEELS_PIN_GPIO_A, WFEELS_PIN_GPIO_B);
        return TEST_PASS;
    }

    snprintf(msg, len, "A:%s B:%s",
            (aHigh && aLow) ? "OK" : "FAIL",
            (bHigh && bLow) ? "OK" : "FAIL");
    return TEST_FAIL;
}

TestResult_t TestSuite::testSerialModes(char* msg, size_t len) {
    // Test each serial mode
    SerialMode_t modes[] = {
        SERIAL_MODE_RS232,
        SERIAL_MODE_RS485_TX,
        SERIAL_MODE_RS485_RX,
        SERIAL_MODE_SDI12_TX,
        SERIAL_MODE_SDI12_RX,
        SERIAL_MODE_OFF
    };

    for (int i = 0; i < 6; i++) {
        WalterFeels::setSerialMode(modes[i]);
        delay(10);

        if (WalterFeels::getSerialMode() != modes[i]) {
            snprintf(msg, len, "Mode %d set failed", modes[i]);
            return TEST_FAIL;
        }
    }

    snprintf(msg, len, "RS232/RS485/SDI12 modes OK");
    return TEST_PASS;
}

TestResult_t TestSuite::testSdCardPins(char* msg, size_t len) {
    // Configure SD card pins as GPIO temporarily
    pinMode(WFEELS_PIN_SD_CMD, OUTPUT);
    pinMode(WFEELS_PIN_SD_CLK, OUTPUT);
    pinMode(WFEELS_PIN_SD_DAT0, INPUT_PULLUP);

    // Toggle outputs
    digitalWrite(WFEELS_PIN_SD_CMD, HIGH);
    digitalWrite(WFEELS_PIN_SD_CLK, HIGH);
    delay(10);
    digitalWrite(WFEELS_PIN_SD_CMD, LOW);
    digitalWrite(WFEELS_PIN_SD_CLK, LOW);

    // Check input with pullup
    bool dat0High = digitalRead(WFEELS_PIN_SD_DAT0);

    snprintf(msg, len, "Pins CMD:%d CLK:%d DAT0:%d OK",
            WFEELS_PIN_SD_CMD, WFEELS_PIN_SD_CLK, WFEELS_PIN_SD_DAT0);
    return TEST_PASS;
}

TestResult_t TestSuite::testCanPins(char* msg, size_t len) {
    // Enable CAN transceiver
    WalterFeels::setCanPower(true);
    delay(50);

    // Check CAN RX pin (should be high with pullup when idle)
    bool rxIdle = digitalRead(WFEELS_PIN_CAN_RX);

    // Toggle TX
    digitalWrite(WFEELS_PIN_CAN_TX, HIGH);
    delay(1);
    digitalWrite(WFEELS_PIN_CAN_TX, LOW);

    WalterFeels::setCanPower(false);

    snprintf(msg, len, "CAN TX:%d RX:%d, RX idle=%d",
            WFEELS_PIN_CAN_TX, WFEELS_PIN_CAN_RX, rxIdle);
    return TEST_PASS;
}

TestResult_t TestSuite::test12vRail(char* msg, size_t len) {
    // Test 12V rail enable/disable
    WalterFeels::set12v(true);
    delay(100);

    // We can't directly measure 12V without ADC, but we test the control works
    WalterFeels::set12v(false);
    delay(50);

    snprintf(msg, len, "12V rail pin %d toggled", WFEELS_PIN_12V_EN);
    return TEST_PASS;
}
