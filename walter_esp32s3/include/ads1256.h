/**
 * @file ads1256.h
 * @brief ADS1256 24-bit ADC driver for ESP32-S3
 *
 * Features:
 * - Hardware interrupt driven DRDY handling
 * - FreeRTOS task notification for zero-latency response
 * - Continuous conversion mode at 1000 SPS
 * - Thread-safe SPI transactions
 */

#ifndef ADS1256_H
#define ADS1256_H

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "config.h"
#include "ring_buffer.h"

// =============================================================================
// ADS1256 CLASS
// =============================================================================

class ADS1256 {
public:
    /**
     * @brief Constructor
     * @param spi SPI bus instance
     */
    ADS1256(SPIClass& spi);

    /**
     * @brief Initialize the ADC
     * @return true if chip ID verified
     */
    bool begin();

    /**
     * @brief Configure ADC parameters
     * @param gain PGA gain (ADS_GAIN_1 to ADS_GAIN_64)
     * @param drate Data rate register value
     */
    void configure(uint8_t gain, uint8_t drate);

    /**
     * @brief Set differential channel
     * @param channel 0-3 for AIN0/1, AIN2/3, AIN4/5, AIN6/7
     */
    void setDiffChannel(uint8_t channel);

    /**
     * @brief Start continuous conversion mode
     */
    void startContinuous();

    /**
     * @brief Stop continuous conversion mode
     */
    void stopContinuous();

    /**
     * @brief Read single sample (blocking, waits for DRDY)
     * @return 24-bit signed value (sign-extended to int32)
     */
    int32_t readSample();

    /**
     * @brief Read chip ID
     * @return Chip ID (should be 3 for ADS1256)
     */
    uint8_t readChipID();

    /**
     * @brief Perform self-calibration
     */
    void selfCalibrate();

    /**
     * @brief Hardware reset
     */
    void reset();

    /**
     * @brief Check if DRDY is low (data ready)
     */
    bool isDataReady();

    /**
     * @brief Start interrupt-driven sampling task
     * @param ringBuffer Destination ring buffer
     * @param taskHandle Output task handle
     */
    void startSamplingTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle);

    /**
     * @brief Stop sampling task
     */
    void stopSamplingTask();

    /**
     * @brief Get sample count
     */
    uint32_t getSampleCount() const { return _sampleCount; }

    /**
     * @brief Get dropped sample count
     */
    uint32_t getDroppedCount() const { return _droppedCount; }

    /**
     * @brief ISR handler - called from DRDY interrupt
     */
    static void IRAM_ATTR drdyISR();

private:
    // SPI communication
    void csLow();
    void csHigh();
    void writeCommand(uint8_t cmd);
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void waitDRDY();

    // Hardware
    SPIClass&       _spi;
    SPISettings     _spiSettings;

    // State
    bool            _inContinuousMode;
    bool            _chipDetected;
    uint8_t         _currentGain;
    uint8_t         _currentDrate;
    uint8_t         _currentChannel;

    // Statistics
    volatile uint32_t _sampleCount;
    volatile uint32_t _droppedCount;

    // Sampling task
    static ADS1256* _instance;
    TaskHandle_t    _samplingTask;
    AdcRingBuffer*  _ringBuffer;
    volatile bool   _stopRequested;

    // ISR notification
    static TaskHandle_t _taskToNotify;
    static volatile bool _drdyFlag;

    // Sampling task function
    static void samplingTaskFunc(void* param);
};

#endif // ADS1256_H
