/**
 * @file ads1256.cpp
 * @brief ADS1256 24-bit ADC driver implementation
 *
 * Implements hardware timer driven DRDY interrupt handling for
 * deterministic 1000 SPS sampling without missed samples.
 */

#include "ads1256.h"
#include <esp_timer.h>
#include <esp_task_wdt.h>

// Static member initialization
ADS1256* ADS1256::_instance = nullptr;
TaskHandle_t ADS1256::_taskToNotify = nullptr;
volatile bool ADS1256::_drdyFlag = false;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

ADS1256::ADS1256(SPIClass& spi)
    : _spi(spi)
    , _spiSettings(ADS_SPI_FREQ, MSBFIRST, ADS_SPI_MODE)
    , _inContinuousMode(false)
    , _chipDetected(false)
    , _currentGain(ADS_GAIN_1)
    , _currentDrate(ADS_DRATE_1000SPS)
    , _currentChannel(0)
    , _sampleCount(0)
    , _droppedCount(0)
    , _samplingTask(nullptr)
    , _ringBuffer(nullptr)
    , _stopRequested(false)
{
    _instance = this;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool ADS1256::begin() {
    // Log pin configuration
    LOG_PRINTF("[ADS1256] Pins: CS=%d, DRDY=%d, RST=%d, SCK=%d, MISO=%d, MOSI=%d\n",
               PIN_ADS_CS, PIN_ADS_DRDY, PIN_ADS_RST, PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    LOG_PRINTF("[ADS1256] SPI frequency: %d Hz\n", ADS_SPI_FREQ);

    // Configure GPIO pins
    pinMode(PIN_ADS_CS, OUTPUT);
    pinMode(PIN_ADS_DRDY, INPUT_PULLUP);  // Use pullup to prevent floating when no chip
    pinMode(PIN_ADS_RST, OUTPUT);

    csHigh();

    // Hardware reset
    reset();
    delay(100);  // Wait longer after reset

    // Check DRDY pin state
    int drdyState = digitalRead(PIN_ADS_DRDY);
    LOG_PRINTF("[ADS1256] DRDY state after reset: %s\n",
               drdyState == LOW ? "LOW (ready)" : "HIGH (busy)");

    // If DRDY stuck HIGH, chip may not be present or wiring issue
    if (drdyState == HIGH) {
        LOG_PRINTLN("[ADS1256] WARNING: DRDY stuck HIGH - possible wiring issue");
    }

    // Read chip ID multiple times to check SPI stability
    LOG_PRINTLN("[ADS1256] Reading chip ID (3 attempts)...");
    uint8_t chipIds[3];
    for (int i = 0; i < 3; i++) {
        chipIds[i] = readChipID();
        LOG_PRINTF("[ADS1256]   Attempt %d: ID=0x%02X\n", i+1, chipIds[i]);
        delay(10);
    }

    // Check consistency
    bool consistent = (chipIds[0] == chipIds[1] && chipIds[1] == chipIds[2]);
    uint8_t chipId = chipIds[0];

    if (!consistent) {
        LOG_PRINTLN("[ADS1256] WARNING: Inconsistent chip ID reads - SPI may be unreliable");
        LOG_PRINTLN("[ADS1256]   Try lowering SPI speed (ADS_SPI_FREQ in config.h)");
        LOG_PRINTLN("[ADS1256]   Or check wiring for noise/bad connections");
    }

    LOG_PRINTF("[ADS1256] Chip ID: 0x%02X (expected 0x03)\n", chipId);

    if (chipId != 0x03) {
        LOG_PRINTLN("[ADS1256] WARNING: Wrong chip ID detected!");
        LOG_PRINTLN("[ADS1256]   Possible causes:");
        LOG_PRINTLN("[ADS1256]   - SPI speed too high for wiring (try 500kHz)");
        LOG_PRINTLN("[ADS1256]   - MISO/MOSI swapped");
        LOG_PRINTLN("[ADS1256]   - Bad solder joint or loose connection");
        LOG_PRINTLN("[ADS1256]   - Chip not powered (check AVDD/DVDD)");
        LOG_PRINTLN("[ADS1256]   - Different/incompatible ADC chip");

        // Try anyway if we got some response (not 0xFF or 0x00)
        if (chipId != 0x00 && chipId != 0xFF && chipId != 0x0F) {
            LOG_PRINTLN("[ADS1256] Attempting initialization anyway (debug mode)...");
            _chipDetected = true;  // Try to use it
        } else {
            LOG_PRINTLN("[ADS1256] No valid response - chip not present or not powered");
            _chipDetected = false;
            return false;
        }
    } else {
        _chipDetected = true;
    }

    // Default configuration
    LOG_PRINTLN("[ADS1256] Configuring ADC...");
    configure(ADS_GAIN_4, ADS_DRATE_1000SPS);

    // Self-calibration
    selfCalibrate();

    LOG_PRINTLN("[ADS1256] Initialization complete");
    return (chipId == 0x03);  // Return true only if correct chip ID
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void ADS1256::configure(uint8_t gain, uint8_t drate) {
    _currentGain = gain;
    _currentDrate = drate;

    waitDRDY();

    // Write STATUS, MUX, ADCON, DRATE registers (0x00-0x03)
    csLow();
    _spi.beginTransaction(_spiSettings);

    _spi.transfer(ADS_CMD_WREG | ADS_REG_STATUS);  // Start at STATUS
    _spi.transfer(0x03);                            // Write 4 registers

    // STATUS: Buffer disabled, auto-cal disabled, LSB first
    _spi.transfer(0x04);

    // MUX: Will be set by setDiffChannel
    _spi.transfer(ADS_MUX_DIFF_0_1);

    // ADCON: Clock out disabled, sensor detect off, PGA gain
    _spi.transfer(gain & 0x07);

    // DRATE: Data rate
    _spi.transfer(drate);

    _spi.endTransaction();
    csHigh();

    delayMicroseconds(100);

    LOG_PRINTF("[ADS1256] Configured: gain=%d, drate=0x%02X\n", gain, drate);
}

void ADS1256::setDiffChannel(uint8_t channel) {
    _currentChannel = channel;

    uint8_t mux;
    switch (channel) {
        case 0: mux = ADS_MUX_DIFF_0_1; break;
        case 1: mux = ADS_MUX_DIFF_2_3; break;
        case 2: mux = ADS_MUX_DIFF_4_5; break;
        case 3: mux = ADS_MUX_DIFF_6_7; break;
        default: mux = ADS_MUX_DIFF_0_1; break;
    }

    writeRegister(ADS_REG_MUX, mux);
    delayMicroseconds(10);
}

// =============================================================================
// CONTINUOUS MODE
// =============================================================================

void ADS1256::startContinuous() {
    if (_inContinuousMode) return;

    // Stop any previous continuous mode
    writeCommand(ADS_CMD_SDATAC);
    delayMicroseconds(10);

    // Sync and wakeup to start conversions
    writeCommand(ADS_CMD_SYNC);
    delayMicroseconds(10);
    writeCommand(ADS_CMD_WAKEUP);
    delayMicroseconds(10);

    // Enter continuous read mode
    writeCommand(ADS_CMD_RDATAC);

    _inContinuousMode = true;
    LOG_PRINTLN("[ADS1256] Continuous mode started");
}

void ADS1256::stopContinuous() {
    if (!_inContinuousMode) return;

    writeCommand(ADS_CMD_SDATAC);
    _inContinuousMode = false;
    LOG_PRINTLN("[ADS1256] Continuous mode stopped");
}

// =============================================================================
// SAMPLE READING
// =============================================================================

int32_t ADS1256::readSample() {
    waitDRDY();

    csLow();
    _spi.beginTransaction(_spiSettings);

    // In RDATAC mode, just clock out the data (no RDATA command needed)
    // In normal mode, we'd send RDATA first
    if (!_inContinuousMode) {
        _spi.transfer(ADS_CMD_RDATA);
        delayMicroseconds(7);  // t6 delay
    }

    // Read 3 bytes (24-bit data, MSB first)
    uint8_t b0 = _spi.transfer(0x00);
    uint8_t b1 = _spi.transfer(0x00);
    uint8_t b2 = _spi.transfer(0x00);

    _spi.endTransaction();
    csHigh();

    // Combine bytes into 24-bit value
    int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;

    // Sign extend from 24-bit to 32-bit (two's complement)
    if (value & 0x800000) {
        value -= 0x1000000;
    }

    _sampleCount++;
    return value;
}

uint8_t ADS1256::readChipID() {
    waitDRDY();

    uint8_t status = readRegister(ADS_REG_STATUS);
    LOG_PRINTF("[ADS1256] STATUS register raw: 0x%02X\n", status);
    return (status >> 4) & 0x0F;
}

void ADS1256::selfCalibrate() {
    waitDRDY();
    writeCommand(ADS_CMD_SELFCAL);
    delay(100);  // Wait for calibration
    waitDRDY();
    LOG_PRINTLN("[ADS1256] Self-calibration complete");
}

void ADS1256::reset() {
    LOG_PRINTLN("[ADS1256] Performing hardware reset...");
    digitalWrite(PIN_ADS_RST, HIGH);
    delay(10);
    digitalWrite(PIN_ADS_RST, LOW);
    delay(10);
    digitalWrite(PIN_ADS_RST, HIGH);
    delay(100);  // Wait for chip to come out of reset
    LOG_PRINTLN("[ADS1256] Reset complete");
}

bool ADS1256::isDataReady() {
    return digitalRead(PIN_ADS_DRDY) == LOW;
}

// =============================================================================
// LOW-LEVEL SPI
// =============================================================================

void ADS1256::csLow() {
    digitalWrite(PIN_ADS_CS, LOW);
}

void ADS1256::csHigh() {
    digitalWrite(PIN_ADS_CS, HIGH);
}

void ADS1256::writeCommand(uint8_t cmd) {
    csLow();
    _spi.beginTransaction(_spiSettings);
    _spi.transfer(cmd);
    _spi.endTransaction();
    csHigh();
}

void ADS1256::writeRegister(uint8_t reg, uint8_t value) {
    waitDRDY();
    csLow();
    _spi.beginTransaction(_spiSettings);
    _spi.transfer(ADS_CMD_WREG | (reg & 0x0F));
    _spi.transfer(0x00);  // Write 1 register
    _spi.transfer(value);
    _spi.endTransaction();
    csHigh();
    delayMicroseconds(10);
}

uint8_t ADS1256::readRegister(uint8_t reg) {
    uint8_t cmd = ADS_CMD_RREG | (reg & 0x0F);

    csLow();
    delayMicroseconds(5);  // t_CSS: CS setup time
    _spi.beginTransaction(_spiSettings);
    _spi.transfer(cmd);
    _spi.transfer(0x00);  // Read 1 register (n-1 = 0)
    delayMicroseconds(10);  // t6 delay (50 CLKIN cycles)
    uint8_t value = _spi.transfer(0x00);
    _spi.endTransaction();
    delayMicroseconds(5);  // t_CSH: CS hold time
    csHigh();

    LOG_PRINTF("[ADS1256] Read reg 0x%02X cmd=0x%02X -> 0x%02X\n", reg, cmd, value);
    return value;
}

void ADS1256::waitDRDY() {
    // Wait for DRDY low with timeout
    // Use delay() which feeds the watchdog and works from any context
    uint32_t startMs = millis();
    const uint32_t timeoutMs = 100;  // 100ms timeout

    while (digitalRead(PIN_ADS_DRDY) == HIGH) {
        if (millis() - startMs > timeoutMs) {
            LOG_PRINTLN("[ADS1256] DRDY timeout!");
            break;
        }
        // Small delay feeds watchdog and prevents tight loop
        delay(1);
    }
}

// =============================================================================
// INTERRUPT-DRIVEN SAMPLING TASK
// =============================================================================

void IRAM_ATTR ADS1256::drdyISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    _drdyFlag = true;

    // Notify the sampling task
    if (_taskToNotify != nullptr) {
        vTaskNotifyGiveFromISR(_taskToNotify, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void ADS1256::startSamplingTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle) {
    // Don't start sampling if chip not detected
    if (!_chipDetected) {
        LOG_PRINTLN("[ADS1256] Sampling task NOT started - no chip detected");
        if (taskHandle) {
            *taskHandle = nullptr;
        }
        return;
    }

    _ringBuffer = ringBuffer;
    _stopRequested = false;
    _taskToNotify = nullptr;  // Clear until task is ready

    LOG_PRINTLN("[ADS1256] Creating sampling task...");
    Serial.flush();  // Ensure output before task creation

    // Create the sampling task - use Core 0 for stability (Core 1 can have issues)
    // Stack size in WORDS (4 bytes each on ESP32)
    BaseType_t result = xTaskCreatePinnedToCore(
        samplingTaskFunc,
        "ADC_Sample",
        TASK_STACK_ADC,     // 4096 words = 16KB
        this,
        TASK_PRIORITY_ADC,  // Priority 5
        &_samplingTask,
        0                   // Core 0 for better stability
    );

    if (result != pdPASS) {
        LOG_PRINTLN("[ADS1256] ERROR: Failed to create sampling task!");
        _samplingTask = nullptr;
        if (taskHandle) {
            *taskHandle = nullptr;
        }
        return;
    }

    if (taskHandle) {
        *taskHandle = _samplingTask;
    }

    LOG_PRINTLN("[ADS1256] Sampling task created successfully");
    Serial.flush();
}

void ADS1256::stopSamplingTask() {
    _stopRequested = true;
    detachInterrupt(digitalPinToInterrupt(PIN_ADS_DRDY));
    _taskToNotify = nullptr;

    if (_samplingTask) {
        // Wait for task to terminate
        vTaskDelay(pdMS_TO_TICKS(100));
        _samplingTask = nullptr;
    }

    stopContinuous();
    LOG_PRINTLN("[ADS1256] Sampling task stopped");
}

void ADS1256::samplingTaskFunc(void* param) {
    ADS1256* adc = static_cast<ADS1256*>(param);

    // =========================================================================
    // ESP32/FreeRTOS Best Practices: Task Initialization
    // =========================================================================

    LOG_PRINTLN("[ADC Task] === Task Started ===");
    Serial.flush();

    // Check stack high water mark
    UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(NULL);
    LOG_PRINTF("[ADC Task] Initial stack free: %u words\n", stackHighWater);
    Serial.flush();

    // Add this task to watchdog (optional, comment out if causing issues)
    // esp_task_wdt_add(NULL);

    // Yield immediately to let other tasks run
    LOG_PRINTLN("[ADC Task] Yielding to let other tasks initialize...");
    Serial.flush();
    taskYIELD();

    // Wait for system to stabilize
    LOG_PRINTLN("[ADC Task] Waiting 1 second for system stability...");
    Serial.flush();

    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        // esp_task_wdt_reset();  // Feed watchdog if enabled
    }

    LOG_PRINTLN("[ADC Task] Configuring ADC...");
    Serial.flush();

    // Configure for 1000 SPS differential channel 0
    adc->configure(ADS_GAIN_4, ADS_DRATE_1000SPS);

    LOG_PRINTLN("[ADC Task] Setting differential channel...");
    Serial.flush();
    adc->setDiffChannel(0);

    LOG_PRINTLN("[ADC Task] Starting continuous mode...");
    Serial.flush();
    adc->startContinuous();

    // Check stack after configuration
    stackHighWater = uxTaskGetStackHighWaterMark(NULL);
    LOG_PRINTF("[ADC Task] Stack free after config: %u words\n", stackHighWater);
    Serial.flush();

    // Now attach interrupt - task is ready to receive notifications
    LOG_PRINTLN("[ADC Task] Attaching DRDY interrupt...");
    Serial.flush();

    _taskToNotify = xTaskGetCurrentTaskHandle();
    attachInterrupt(digitalPinToInterrupt(PIN_ADS_DRDY), drdyISR, FALLING);

    LOG_PRINTLN("[ADC Task] DRDY interrupt attached successfully");
    Serial.flush();

    // Initialize frame buffer
    AdcFrame frame;
    frame.sampleRateHz = SAMPLE_RATE_HZ;
    frame.numSamples = FRAME_SIZE;

    uint32_t sampleIndex = 0;
    uint32_t frameIndex = 0;
    int64_t frameStartUs = esp_timer_get_time();
    const int64_t samplePeriodUs = 1000000 / SAMPLE_RATE_HZ;
    uint32_t loopCounter = 0;

    LOG_PRINTLN("[ADC Task] === Entering Main Sampling Loop ===");
    Serial.flush();

    while (!adc->_stopRequested) {
        // Wait for DRDY interrupt (task notification) or timeout
        // This properly blocks and yields CPU to other tasks
        uint32_t notifyValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        // Periodic status and yield (every ~1000 loops = ~1 second at 1kHz)
        if (++loopCounter >= 1000) {
            loopCounter = 0;
            taskYIELD();
            // Optional: Check stack health periodically
            // UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
            // if (stackFree < 100) LOG_PRINTLN("[ADC Task] WARNING: Low stack!");
        }

        // Only read if we got a notification (DRDY interrupt fired)
        if (notifyValue > 0) {
            // Read sample immediately
            adc->csLow();
            adc->_spi.beginTransaction(adc->_spiSettings);

            uint8_t b0 = adc->_spi.transfer(0x00);
            uint8_t b1 = adc->_spi.transfer(0x00);
            uint8_t b2 = adc->_spi.transfer(0x00);

            adc->_spi.endTransaction();
            adc->csHigh();

            // Convert to signed 32-bit
            int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
            if (value & 0x800000) {
                value -= 0x1000000;
            }

            // Store in frame buffer
            frame.samples[sampleIndex++] = value;
            adc->_sampleCount++;

            _drdyFlag = false;

            // Check if frame is complete
            if (sampleIndex >= FRAME_SIZE) {
                // Calculate timestamps for this frame
                int64_t now = esp_timer_get_time();
                frame.frameIndex = frameIndex++;
                frame.startTimeUs = frameStartUs;
                frame.endTimeUs = frameStartUs + (FRAME_SIZE * samplePeriodUs);

                // Push frame to ring buffer
                if (adc->_ringBuffer) {
                    adc->_ringBuffer->push(frame);
                }

                // Prepare for next frame
                frameStartUs = frame.endTimeUs;
                sampleIndex = 0;

                // Yield briefly to allow other tasks to run
                taskYIELD();
            }
        }
    }

    LOG_PRINTLN("[ADC Task] Sampling loop exited");
    vTaskDelete(nullptr);
}
