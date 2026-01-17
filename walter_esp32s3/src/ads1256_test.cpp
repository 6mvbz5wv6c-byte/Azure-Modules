/**
 * @file ads1256_test.cpp
 * @brief ADS1256 diagnostic test - high debug verbosity
 *
 * Standalone diagnostic to troubleshoot ADS1256 communication issues.
 * Tests multiple SPI speeds, reads all registers, attempts sampling
 * even with wrong chip ID.
 *
 * To use: Rename main.cpp to main.cpp.bak and this to main.cpp
 * Or use build flags to exclude main.cpp
 */

#include <Arduino.h>
#include <SPI.h>
#include "config.h"

// =============================================================================
// TEST CONFIGURATION
// =============================================================================

// SPI speeds to test (Hz) - from slow to fast
const uint32_t SPI_SPEEDS[] = {
    100000,     // 100 kHz - very conservative
    250000,     // 250 kHz - safe
    500000,     // 500 kHz - moderate
    1000000,    // 1 MHz - typical
    1500000,    // 1.5 MHz
    2000000,    // 2 MHz - current config (potentially too fast)
    2500000     // 2.5 MHz - ADS1256 max (risky)
};
const int NUM_SPEEDS = sizeof(SPI_SPEEDS) / sizeof(SPI_SPEEDS[0]);

// Current test speed
SPISettings currentSpiSettings(500000, MSBFIRST, SPI_MODE1);
SPIClass* spi = nullptr;

// =============================================================================
// LOW LEVEL SPI FUNCTIONS
// =============================================================================

void csLow() {
    digitalWrite(PIN_ADS_CS, LOW);
    delayMicroseconds(2);  // CS setup time
}

void csHigh() {
    delayMicroseconds(2);  // CS hold time
    digitalWrite(PIN_ADS_CS, HIGH);
}

uint8_t readRegister(uint8_t reg) {
    uint8_t cmd = ADS_CMD_RREG | (reg & 0x0F);

    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(cmd);
    spi->transfer(0x00);  // Read 1 register (n-1 = 0)
    delayMicroseconds(10);  // t6 delay (50 CLKIN cycles at 7.68MHz = 6.5us)
    uint8_t value = spi->transfer(0x00);

    spi->endTransaction();
    csHigh();

    return value;
}

void writeRegister(uint8_t reg, uint8_t value) {
    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(ADS_CMD_WREG | (reg & 0x0F));
    spi->transfer(0x00);  // Write 1 register
    spi->transfer(value);

    spi->endTransaction();
    csHigh();
    delayMicroseconds(10);
}

void sendCommand(uint8_t cmd) {
    csLow();
    spi->beginTransaction(currentSpiSettings);
    spi->transfer(cmd);
    spi->endTransaction();
    csHigh();
}

void waitDRDY(uint32_t timeoutMs = 500) {
    uint32_t start = millis();
    while (digitalRead(PIN_ADS_DRDY) == HIGH) {
        if (millis() - start > timeoutMs) {
            Serial.println("    [TIMEOUT] DRDY never went low!");
            return;
        }
        delayMicroseconds(100);
    }
}

void hardwareReset() {
    Serial.println("\n  [RESET] Performing hardware reset...");
    digitalWrite(PIN_ADS_RST, LOW);
    delay(10);
    digitalWrite(PIN_ADS_RST, HIGH);
    delay(100);  // Wait for oscillator startup
    Serial.printf("    DRDY after reset: %s\n",
                  digitalRead(PIN_ADS_DRDY) == LOW ? "LOW (ready)" : "HIGH (busy)");
}

void softwareReset() {
    Serial.println("  [RESET] Sending software reset command (0xFE)...");
    sendCommand(ADS_CMD_RESET);
    delay(100);
}

// =============================================================================
// REGISTER DUMP
// =============================================================================

const char* REG_NAMES[] = {
    "STATUS", "MUX", "ADCON", "DRATE", "IO",
    "OFC0", "OFC1", "OFC2", "FSC0", "FSC1", "FSC2"
};

void dumpAllRegisters() {
    Serial.println("\n  [REGISTERS] Reading all registers:");
    Serial.println("    Reg  Name    Hex    Binary      Description");
    Serial.println("    ---  ------  ----   --------    -----------");

    for (int i = 0; i <= 0x0A; i++) {
        waitDRDY(100);
        uint8_t val = readRegister(i);

        // Binary string
        char binStr[9];
        for (int b = 7; b >= 0; b--) {
            binStr[7-b] = (val & (1 << b)) ? '1' : '0';
        }
        binStr[8] = '\0';

        Serial.printf("    0x%02X %-7s 0x%02X   %s", i, REG_NAMES[i], val, binStr);

        // Decode specific registers
        if (i == ADS_REG_STATUS) {
            uint8_t id = (val >> 4) & 0x0F;
            Serial.printf("    ID=%d (expect 3), ORDER=%d, ACAL=%d, BUFEN=%d, DRDY=%d",
                         id, (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
            if (id != 0x03) {
                Serial.print(" *** WRONG ID ***");
            }
        } else if (i == ADS_REG_MUX) {
            Serial.printf("    PSEL=%d, NSEL=%d (AIN%d - AIN%d)",
                         (val>>4)&0xF, val&0xF, (val>>4)&0xF, val&0xF);
        } else if (i == ADS_REG_ADCON) {
            Serial.printf("    CLK=%d, SDCS=%d, PGA=%d (gain=%d)",
                         (val>>5)&3, (val>>3)&3, val&7, 1<<(val&7));
        } else if (i == ADS_REG_DRATE) {
            const char* rate = "unknown";
            switch(val) {
                case 0xF0: rate = "30000 SPS"; break;
                case 0xE0: rate = "15000 SPS"; break;
                case 0xD0: rate = "7500 SPS"; break;
                case 0xC0: rate = "3750 SPS"; break;
                case 0xB0: rate = "2000 SPS"; break;
                case 0xA1: rate = "1000 SPS"; break;
                case 0x92: rate = "500 SPS"; break;
                case 0x82: rate = "100 SPS"; break;
                case 0x72: rate = "60 SPS"; break;
                case 0x63: rate = "50 SPS"; break;
                case 0x53: rate = "30 SPS"; break;
                case 0x43: rate = "25 SPS"; break;
                case 0x33: rate = "15 SPS"; break;
                case 0x23: rate = "10 SPS"; break;
                case 0x13: rate = "5 SPS"; break;
                case 0x03: rate = "2.5 SPS"; break;
            }
            Serial.printf("    %s", rate);
        }
        Serial.println();
    }
}

// =============================================================================
// SAMPLE READING TEST
// =============================================================================

int32_t readSingleSample() {
    waitDRDY(100);

    csLow();
    spi->beginTransaction(currentSpiSettings);

    spi->transfer(ADS_CMD_RDATA);
    delayMicroseconds(10);  // t6 delay

    uint8_t b0 = spi->transfer(0x00);
    uint8_t b1 = spi->transfer(0x00);
    uint8_t b2 = spi->transfer(0x00);

    spi->endTransaction();
    csHigh();

    // Combine to 24-bit, sign extend to 32-bit
    int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
    if (value & 0x800000) {
        value |= 0xFF000000;  // Sign extend
    }

    return value;
}

void testSampling(int numSamples) {
    Serial.printf("\n  [SAMPLING] Reading %d samples...\n", numSamples);

    // Configure for 100 SPS (slow, reliable for testing)
    writeRegister(ADS_REG_DRATE, ADS_DRATE_100SPS);
    writeRegister(ADS_REG_MUX, ADS_MUX_DIFF_0_1);  // AIN0-AIN1
    writeRegister(ADS_REG_ADCON, ADS_GAIN_1);      // Gain = 1

    // Self-calibrate
    Serial.println("    Running self-calibration...");
    waitDRDY(100);
    sendCommand(ADS_CMD_SELFCAL);
    delay(200);

    // Sync and wakeup
    sendCommand(ADS_CMD_SYNC);
    delayMicroseconds(10);
    sendCommand(ADS_CMD_WAKEUP);
    delay(10);

    Serial.println("    Sample   Raw Value    Hex          Voltage (approx)");
    Serial.println("    ------   ---------    ----------   ----------------");

    int32_t minVal = INT32_MAX, maxVal = INT32_MIN;
    int64_t sum = 0;
    int validSamples = 0;

    for (int i = 0; i < numSamples; i++) {
        int32_t sample = readSingleSample();

        // Check for stuck values (all 1s or all 0s = communication error)
        bool stuck = (sample == 0x7FFFFF || sample == -0x800000 ||
                     sample == 0 || sample == -1);

        // Approximate voltage (assuming Vref=2.5V, gain=1, differential)
        // Full scale = +/- Vref/Gain = +/- 2.5V = +/- 8388607 counts
        float voltage = (sample / 8388607.0f) * 2.5f;

        Serial.printf("    %4d     %8d     0x%06X     %+.6f V%s\n",
                      i, sample, sample & 0xFFFFFF, voltage,
                      stuck ? " [STUCK?]" : "");

        if (!stuck) {
            if (sample < minVal) minVal = sample;
            if (sample > maxVal) maxVal = sample;
            sum += sample;
            validSamples++;
        }

        delay(15);  // ~100 SPS = 10ms per sample, add margin
    }

    if (validSamples > 0) {
        float avg = (float)sum / validSamples;
        Serial.printf("\n    Statistics: min=%d, max=%d, avg=%.1f, range=%d\n",
                      minVal, maxVal, avg, maxVal - minVal);
        Serial.printf("    Valid samples: %d/%d (%.1f%%)\n",
                      validSamples, numSamples, 100.0f * validSamples / numSamples);

        // Check for noise level (should be < 100 counts at gain=1)
        if (maxVal - minVal < 100) {
            Serial.println("    Noise level: GOOD (< 100 counts)");
        } else if (maxVal - minVal < 1000) {
            Serial.println("    Noise level: MODERATE (100-1000 counts)");
        } else {
            Serial.println("    Noise level: HIGH (> 1000 counts) - check wiring/grounding");
        }
    } else {
        Serial.println("\n    [ERROR] No valid samples collected!");
    }
}

// =============================================================================
// SPI SPEED TEST
// =============================================================================

bool testSpiSpeed(uint32_t speedHz) {
    Serial.printf("\n======================================================\n");
    Serial.printf("TESTING SPI SPEED: %lu Hz (%.2f MHz)\n", speedHz, speedHz / 1000000.0f);
    Serial.printf("======================================================\n");

    currentSpiSettings = SPISettings(speedHz, MSBFIRST, SPI_MODE1);

    // Hardware reset before each test
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
        Serial.println("    [WARNING] Inconsistent readings - SPI may be unreliable at this speed");
    } else {
        Serial.println("    [OK] Readings are consistent");
    }

    // Check chip ID
    uint8_t chipId = (readings[0] >> 4) & 0x0F;
    bool correctId = (chipId == 0x03);

    if (correctId) {
        Serial.println("\n  [CHIP ID] CORRECT! ADS1256 detected (ID=0x03)");
    } else {
        Serial.printf("\n  [CHIP ID] WRONG! Got 0x%02X, expected 0x03\n", chipId);
        Serial.println("    Possible causes:");
        Serial.println("    - SPI speed too high for wiring");
        Serial.println("    - Incorrect SPI mode");
        Serial.println("    - Wiring issue (MISO/MOSI swapped, bad connection)");
        Serial.println("    - Chip not powered properly");
        Serial.println("    - Different/fake chip");
        Serial.println("    --> Continuing anyway to gather more data...");
    }

    // Dump all registers
    dumpAllRegisters();

    // Try write/read test
    Serial.println("\n  [WRITE TEST] Writing and reading back DRATE register:");
    uint8_t testValues[] = {0xA1, 0x82, 0x03, 0xF0};  // Different data rates
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
        Serial.println("    [OK] All write/read tests passed");
    } else {
        Serial.println("    [FAIL] Write/read mismatch - SPI communication unreliable");
    }

    // Try sampling if write test passed
    if (writeTestPassed || true) {  // Always try sampling for diagnosis
        testSampling(10);
    }

    return correctId && consistent && writeTestPassed;
}

// =============================================================================
// MAIN TEST ENTRY POINT
// =============================================================================

void runAdsDiagnostic() {
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
    Serial.printf("  SPI Mode: %d (CPOL=%d, CPHA=%d)\n",
                  ADS_SPI_MODE, (ADS_SPI_MODE >> 1) & 1, ADS_SPI_MODE & 1);

    // Initialize SPI
    Serial.println("\n[INIT] Initializing SPI bus...");
    spi = new SPIClass(HSPI);
    spi->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_ADS_CS);

    // Initialize GPIO
    pinMode(PIN_ADS_CS, OUTPUT);
    pinMode(PIN_ADS_DRDY, INPUT_PULLUP);
    pinMode(PIN_ADS_RST, OUTPUT);
    digitalWrite(PIN_ADS_CS, HIGH);
    digitalWrite(PIN_ADS_RST, HIGH);

    // Check DRDY state before any reset
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
    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║                     DIAGNOSTIC SUMMARY                       ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");

    if (bestSpeed >= 0) {
        Serial.printf("\n[RESULT] Lowest reliable speed: %lu Hz (%.2f MHz)\n",
                      SPI_SPEEDS[bestSpeed], SPI_SPEEDS[bestSpeed] / 1000000.0f);
        Serial.println("[RECOMMENDATION] Use this speed or lower in config.h:");
        Serial.printf("  #define ADS_SPI_FREQ    %lu\n", SPI_SPEEDS[bestSpeed]);
    } else {
        Serial.println("\n[RESULT] No SPI speed produced correct chip ID!");
        Serial.println("\n[TROUBLESHOOTING]:");
        Serial.println("  1. Check wiring - especially MISO/MOSI orientation");
        Serial.println("  2. Verify ADS1256 is powered (AVDD=5V, DVDD=3.3V or 5V)");
        Serial.println("  3. Check for cold solder joints");
        Serial.println("  4. Try adding 10-100 ohm series resistors on SPI lines");
        Serial.println("  5. Reduce wire length or use shielded cables");
        Serial.println("  6. Add 100nF decoupling caps near ADS1256 power pins");
    }

    Serial.println("\n[DONE] Diagnostic complete. Looping with slow reads...\n");

    // Continuous slow monitoring
    currentSpiSettings = SPISettings(250000, MSBFIRST, SPI_MODE1);  // Very slow
    hardwareReset();

    while (true) {
        waitDRDY(500);
        uint8_t status = readRegister(ADS_REG_STATUS);
        int32_t sample = readSingleSample();
        Serial.printf("[MONITOR] STATUS=0x%02X (ID=%d), Sample=%d (0x%06X)\n",
                      status, (status >> 4) & 0x0F, sample, sample & 0xFFFFFF);
        delay(1000);
    }
}

// =============================================================================
// ARDUINO SETUP/LOOP (when used as standalone test)
// =============================================================================

#ifdef ADS_DIAGNOSTIC_TEST_STANDALONE

void setup() {
    Serial.begin(921600);
    while (!Serial && millis() < 3000);  // Wait for USB serial
    delay(1000);

    Serial.println("\n\n========================================");
    Serial.println("ADS1256 Diagnostic Test Starting...");
    Serial.println("========================================\n");

    runAdsDiagnostic();
}

void loop() {
    // Diagnostic runs in setup, loop does nothing
}

#endif
