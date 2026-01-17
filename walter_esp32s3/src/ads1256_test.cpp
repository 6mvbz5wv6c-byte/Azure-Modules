/**
 * @file ads1256_test.cpp
 * @brief ADS1256 diagnostic test - high debug verbosity
 *
 * Standalone diagnostic to troubleshoot ADS1256 communication issues.
 * Tests multiple SPI speeds, reads all registers, attempts sampling
 * even with wrong chip ID.
 *
 * Called via 'd' serial command from main.cpp
 */

#include <Arduino.h>
#include <SPI.h>
#include "config.h"

// =============================================================================
// Wrap everything in a namespace to avoid conflicts with main ADS1256 driver
// =============================================================================
namespace AdsDiag {

// SPI speeds to test (Hz) - from slow to fast
static const uint32_t SPI_SPEEDS[] = {
    100000,     // 100 kHz - very conservative
    250000,     // 250 kHz - safe
    500000,     // 500 kHz - moderate
    1000000,    // 1 MHz - typical
    1500000,    // 1.5 MHz
    2000000,    // 2 MHz - (potentially too fast)
};
static const int NUM_SPEEDS = sizeof(SPI_SPEEDS) / sizeof(SPI_SPEEDS[0]);

// Current test settings - use MODE1 as per ADS1256 datasheet
static SPISettings currentSpiSettings(500000, MSBFIRST, SPI_MODE1);
static SPIClass* spi = nullptr;

// =============================================================================
// LOW LEVEL SPI FUNCTIONS
// =============================================================================

static void csLow() {
    digitalWrite(PIN_ADS_CS, LOW);
    delayMicroseconds(2);
}

static void csHigh() {
    delayMicroseconds(2);
    digitalWrite(PIN_ADS_CS, HIGH);
}

static uint8_t readRegister(uint8_t reg) {
    uint8_t cmd = ADS_CMD_RREG | (reg & 0x0F);

    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(cmd);
    spi->transfer(0x00);
    delayMicroseconds(10);
    uint8_t value = spi->transfer(0x00);

    spi->endTransaction();
    csHigh();

    return value;
}

static void writeRegister(uint8_t reg, uint8_t value) {
    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(ADS_CMD_WREG | (reg & 0x0F));
    spi->transfer(0x00);
    spi->transfer(value);

    spi->endTransaction();
    csHigh();
    delayMicroseconds(10);
}

static void sendCommand(uint8_t cmd) {
    csLow();
    spi->beginTransaction(currentSpiSettings);
    spi->transfer(cmd);
    spi->endTransaction();
    csHigh();
}

static void waitDRDY(uint32_t timeoutMs = 500) {
    uint32_t start = millis();
    while (digitalRead(PIN_ADS_DRDY) == HIGH) {
        if (millis() - start > timeoutMs) {
            Serial.println("    [TIMEOUT] DRDY never went low!");
            return;
        }
        delayMicroseconds(100);
    }
}

static void hardwareReset() {
    Serial.println("\n  [RESET] Performing hardware reset...");
    digitalWrite(PIN_ADS_RST, LOW);
    delay(10);
    digitalWrite(PIN_ADS_RST, HIGH);
    delay(100);
    Serial.printf("    DRDY after reset: %s\n",
                  digitalRead(PIN_ADS_DRDY) == LOW ? "LOW (ready)" : "HIGH (busy)");
}

// =============================================================================
// REGISTER DUMP
// =============================================================================

static const char* REG_NAMES[] = {
    "STATUS", "MUX", "ADCON", "DRATE", "IO",
    "OFC0", "OFC1", "OFC2", "FSC0", "FSC1", "FSC2"
};

static void dumpAllRegisters() {
    Serial.println("\n  [REGISTERS] Reading all registers:");
    Serial.println("    Reg  Name    Hex    Binary");
    Serial.println("    ---  ------  ----   --------");

    for (int i = 0; i <= 0x0A; i++) {
        waitDRDY(100);
        uint8_t val = readRegister(i);

        char binStr[9];
        for (int b = 7; b >= 0; b--) {
            binStr[7-b] = (val & (1 << b)) ? '1' : '0';
        }
        binStr[8] = '\0';

        Serial.printf("    0x%02X %-7s 0x%02X   %s", i, REG_NAMES[i], val, binStr);

        if (i == ADS_REG_STATUS) {
            uint8_t id = (val >> 4) & 0x0F;
            Serial.printf("  ID=%d%s", id, id != 0x03 ? " ***WRONG***" : " (OK)");
        }
        Serial.println();
    }
}

// =============================================================================
// SAMPLE READING TEST
// =============================================================================

static int32_t readSingleSample() {
    waitDRDY(100);

    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(ADS_CMD_RDATA);
    delayMicroseconds(10);

    uint8_t b0 = spi->transfer(0x00);
    uint8_t b1 = spi->transfer(0x00);
    uint8_t b2 = spi->transfer(0x00);

    spi->endTransaction();
    csHigh();

    int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
    if (value & 0x800000) {
        value |= 0xFF000000;
    }

    return value;
}

static void testSampling(int numSamples) {
    Serial.printf("\n  [SAMPLING] Reading %d samples...\n", numSamples);

    writeRegister(ADS_REG_DRATE, ADS_DRATE_100SPS);
    writeRegister(ADS_REG_MUX, ADS_MUX_DIFF_0_1);
    writeRegister(ADS_REG_ADCON, ADS_GAIN_1);

    Serial.println("    Running self-calibration...");
    waitDRDY(100);
    sendCommand(ADS_CMD_SELFCAL);
    delay(200);

    sendCommand(ADS_CMD_SYNC);
    delayMicroseconds(10);
    sendCommand(ADS_CMD_WAKEUP);
    delay(10);

    Serial.println("    Sample   Raw Value    Voltage (approx)");
    Serial.println("    ------   ---------    ----------------");

    int32_t minVal = INT32_MAX, maxVal = INT32_MIN;
    int validSamples = 0;

    for (int i = 0; i < numSamples; i++) {
        int32_t sample = readSingleSample();
        bool stuck = (sample == 0x7FFFFF || sample == -0x800000 || sample == 0);
        float voltage = (sample / 8388607.0f) * 2.5f;

        Serial.printf("    %4d     %8d     %+.6f V%s\n",
                      i, sample, voltage, stuck ? " [STUCK?]" : "");

        if (!stuck) {
            if (sample < minVal) minVal = sample;
            if (sample > maxVal) maxVal = sample;
            validSamples++;
        }
        delay(15);
    }

    if (validSamples > 0) {
        Serial.printf("\n    Valid: %d/%d, Range: %d counts\n",
                      validSamples, numSamples, maxVal - minVal);
    }
}

// =============================================================================
// SPI SPEED TEST
// =============================================================================

static bool testSpiSpeed(uint32_t speedHz) {
    Serial.printf("\n══════════════════════════════════════════════════════════════\n");
    Serial.printf("TESTING SPI SPEED: %lu Hz (%.2f MHz)\n", speedHz, speedHz / 1000000.0f);
    Serial.printf("══════════════════════════════════════════════════════════════\n");

    currentSpiSettings = SPISettings(speedHz, MSBFIRST, SPI_MODE1);

    hardwareReset();

    // Read STATUS register multiple times to check consistency
    Serial.println("\n  [CONSISTENCY] Reading STATUS register 5 times:");
    uint8_t readings[5];
    bool consistent = true;

    for (int i = 0; i < 5; i++) {
        waitDRDY(100);
        readings[i] = readRegister(ADS_REG_STATUS);
        Serial.printf("    Read %d: 0x%02X (ID=%d)\n", i+1, readings[i], (readings[i] >> 4) & 0x0F);
        if (i > 0 && readings[i] != readings[0]) {
            consistent = false;
        }
        delay(10);
    }

    if (!consistent) {
        Serial.println("    [WARNING] Inconsistent readings!");
    } else {
        Serial.println("    [OK] Readings consistent");
    }

    uint8_t chipId = (readings[0] >> 4) & 0x0F;
    bool correctId = (chipId == 0x03);

    if (correctId) {
        Serial.println("\n  [CHIP ID] CORRECT! ADS1256 detected");
    } else {
        Serial.printf("\n  [CHIP ID] WRONG! Got 0x%02X, expected 0x03\n", chipId);
    }

    dumpAllRegisters();

    // Write/read test
    Serial.println("\n  [WRITE TEST] Testing register write/read:");
    uint8_t testValues[] = {0xA1, 0x82, 0x03, 0xF0};
    bool writeTestPassed = true;

    for (int i = 0; i < 4; i++) {
        waitDRDY(100);
        writeRegister(ADS_REG_DRATE, testValues[i]);
        delay(5);
        waitDRDY(100);
        uint8_t readBack = readRegister(ADS_REG_DRATE);
        bool match = (readBack == testValues[i]);
        Serial.printf("    Write 0x%02X, Read 0x%02X - %s\n",
                      testValues[i], readBack, match ? "PASS" : "FAIL");
        if (!match) writeTestPassed = false;
    }

    if (writeTestPassed) {
        testSampling(5);
    }

    return correctId && consistent && writeTestPassed;
}

} // namespace AdsDiag

// =============================================================================
// PUBLIC ENTRY POINT (called from main.cpp)
// =============================================================================

void runAdsDiagnostic() {
    using namespace AdsDiag;

    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║          ADS1256 DIAGNOSTIC TEST - HIGH DEBUG MODE           ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");

    Serial.println("\n[CONFIG] Pin Configuration:");
    Serial.printf("  MOSI:  GPIO%d\n", PIN_SPI_MOSI);
    Serial.printf("  MISO:  GPIO%d\n", PIN_SPI_MISO);
    Serial.printf("  SCK:   GPIO%d\n", PIN_SPI_SCK);
    Serial.printf("  CS:    GPIO%d\n", PIN_ADS_CS);
    Serial.printf("  DRDY:  GPIO%d\n", PIN_ADS_DRDY);
    Serial.printf("  RST:   GPIO%d\n", PIN_ADS_RST);
    Serial.println("  SPI Mode: MODE1 (CPOL=0, CPHA=1)");

    // Initialize SPI for diagnostic (separate from main driver)
    Serial.println("\n[INIT] Initializing SPI bus for diagnostic...");
    spi = new SPIClass(HSPI);
    spi->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_ADS_CS);

    pinMode(PIN_ADS_CS, OUTPUT);
    pinMode(PIN_ADS_DRDY, INPUT_PULLUP);
    pinMode(PIN_ADS_RST, OUTPUT);
    digitalWrite(PIN_ADS_CS, HIGH);
    digitalWrite(PIN_ADS_RST, HIGH);

    Serial.printf("\n[GPIO] Initial DRDY state: %s\n",
                  digitalRead(PIN_ADS_DRDY) == LOW ? "LOW" : "HIGH");

    // Test each SPI speed
    int bestSpeed = -1;
    for (int i = 0; i < NUM_SPEEDS; i++) {
        bool passed = testSpiSpeed(SPI_SPEEDS[i]);
        if (passed && bestSpeed < 0) {
            bestSpeed = i;
        }
        delay(500);
    }

    // Summary
    Serial.println("\n\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║                     DIAGNOSTIC SUMMARY                       ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");

    if (bestSpeed >= 0) {
        Serial.printf("\n[RESULT] Best working speed: %lu Hz (%.2f MHz)\n",
                      SPI_SPEEDS[bestSpeed], SPI_SPEEDS[bestSpeed] / 1000000.0f);
        Serial.println("[RECOMMENDATION] Update config.h with:");
        Serial.printf("  #define ADS_SPI_FREQ    %lu\n", SPI_SPEEDS[bestSpeed]);
    } else {
        Serial.println("\n[RESULT] No SPI speed produced correct chip ID!");
        Serial.println("\n[TROUBLESHOOTING]:");
        Serial.println("  1. Check wiring - especially MISO/MOSI orientation");
        Serial.println("  2. Verify ADS1256 is powered (AVDD=5V, DVDD=3.3V or 5V)");
        Serial.println("  3. Check for cold solder joints");
    }

    Serial.println("\n[DONE] Diagnostic complete. Press 'r' to restart device.\n");

    // Clean up and return (don't loop forever)
    delete spi;
    spi = nullptr;
}
