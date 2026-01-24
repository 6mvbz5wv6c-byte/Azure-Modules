/**
 * @file test_suite.h
 * @brief Comprehensive Hardware Test Suite for Walter Feels
 *
 * Provides systematic testing of all onboard features with pass/fail reporting.
 */

#ifndef TEST_SUITE_H
#define TEST_SUITE_H

#include <Arduino.h>
#include "config.h"
#include "walter_feels.h"
#include "sensors.h"
#include "ssd1306.h"

// Maximum number of tests
#define MAX_TESTS 20

// Test function signature
typedef TestResult_t (*TestFunc_t)(char* message, size_t msgLen);

// Test registration structure
typedef struct {
    const char* name;
    const char* description;
    TestFunc_t func;
    TestResult_t result;
    char message[64];
    uint32_t durationMs;
} TestEntry_t;

class TestSuite {
public:
    TestSuite(SSD1306& display);

    // Test registration
    void registerTest(const char* name, const char* desc, TestFunc_t func);

    // Run tests
    void runAll();
    void runTest(uint8_t index);

    // Results
    uint8_t getPassCount();
    uint8_t getFailCount();
    uint8_t getSkipCount();
    uint8_t getTotalCount();
    bool allPassed();

    // Reporting
    void printSummary();
    void printDetailedReport();
    void displaySummary();

    // Individual test functions (static for registration)
    static TestResult_t testI2cBusScan(char* msg, size_t len);
    static TestResult_t testOledDisplay(char* msg, size_t len);
    static TestResult_t testHdc1080(char* msg, size_t len);
    static TestResult_t testLps22hb(char* msg, size_t len);
    static TestResult_t testScd30(char* msg, size_t len);
    static TestResult_t testLtc4015(char* msg, size_t len);
    static TestResult_t testPowerRails(char* msg, size_t len);
    static TestResult_t testGpioOutput(char* msg, size_t len);
    static TestResult_t testSerialModes(char* msg, size_t len);
    static TestResult_t testSdCardPins(char* msg, size_t len);
    static TestResult_t testCanPins(char* msg, size_t len);
    static TestResult_t test12vRail(char* msg, size_t len);

private:
    SSD1306& _display;
    TestEntry_t _tests[MAX_TESTS];
    uint8_t _testCount;

    void _logResult(const char* name, TestResult_t result, const char* msg);
    const char* _resultToString(TestResult_t result);
};

// Global test suite instance (for static test functions)
extern TestSuite* g_testSuite;
extern HDC1080* g_hdc1080;
extern LPS22HB* g_lps22hb;
extern SCD30* g_scd30;
extern LTC4015* g_ltc4015;
extern SSD1306* g_display;

#endif // TEST_SUITE_H
