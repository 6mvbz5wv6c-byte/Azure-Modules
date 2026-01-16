/**
 * @file azure_iot.h
 * @brief Azure IoT Hub client for Walter ESP32-S3 over LTE
 *
 * Uses the Walter modem (Sequans GM02SP) for MQTTS connection to Azure IoT Hub.
 * Implements SAS token generation and telemetry publishing.
 */

#ifndef AZURE_IOT_H
#define AZURE_IOT_H

#include <Arduino.h>
#include <WalterModem.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "ring_buffer.h"

// =============================================================================
// AZURE IOT HUB CLIENT
// =============================================================================

class AzureIoTClient {
public:
    /**
     * @brief Constructor
     * @param modem Reference to Walter modem instance
     */
    AzureIoTClient(WalterModem& modem);

    /**
     * @brief Initialize the Azure IoT client
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Connect to Azure IoT Hub via LTE/MQTTS
     * @return true if connected
     */
    bool connect();

    /**
     * @brief Disconnect from Azure IoT Hub
     */
    void disconnect();

    /**
     * @brief Check if connected to Azure IoT Hub
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Publish ADC frame as telemetry
     * @param frame ADC frame to publish
     * @return true if published successfully
     */
    bool publishFrame(const AdcFrame& frame);

    /**
     * @brief Start telemetry task
     * @param ringBuffer Ring buffer to read frames from
     * @param taskHandle Output task handle
     */
    void startTelemetryTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle);

    /**
     * @brief Stop telemetry task
     */
    void stopTelemetryTask();

    /**
     * @brief Get publish count
     */
    uint32_t getPublishCount() const { return _publishCount; }

    /**
     * @brief Get error count
     */
    uint32_t getErrorCount() const { return _errorCount; }

    /**
     * @brief Get LTE signal quality (RSSI)
     */
    int getSignalQuality() const { return _rssi; }

private:
    /**
     * @brief Generate SAS token for Azure IoT Hub authentication
     * @param resourceUri Resource URI (hostname/devices/deviceId)
     * @param key Shared access key (base64)
     * @param expiryTime Expiry time (Unix timestamp)
     * @param output Output buffer for SAS token
     * @param outputLen Output buffer length
     * @return true if successful
     */
    bool generateSasToken(const char* resourceUri, const char* key,
                          uint32_t expiryTime, char* output, size_t outputLen);

    /**
     * @brief HMAC-SHA256 computation
     */
    bool hmacSha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* data, size_t dataLen,
                    uint8_t* output);

    /**
     * @brief Base64 encode
     */
    size_t base64Encode(const uint8_t* input, size_t inputLen,
                        char* output, size_t outputLen);

    /**
     * @brief Base64 decode
     */
    size_t base64Decode(const char* input, size_t inputLen,
                        uint8_t* output, size_t outputLen);

    /**
     * @brief URL encode
     */
    void urlEncode(const char* input, char* output, size_t outputLen);

    // Modem reference
    WalterModem&    _modem;

    // Connection state
    bool            _connected;
    bool            _lteConnected;
    uint32_t        _sasExpiry;
    char            _sasToken[512];

    // Statistics
    volatile uint32_t _publishCount;
    volatile uint32_t _errorCount;
    int             _rssi;

    // Telemetry task
    TaskHandle_t    _telemetryTask;
    AdcRingBuffer*  _ringBuffer;
    volatile bool   _stopRequested;

    // Task function
    static void telemetryTaskFunc(void* param);

    // JSON/Base64 encoding buffer
    static constexpr size_t PAYLOAD_BUFFER_SIZE = 8192;
    char* _payloadBuffer;
};

#endif // AZURE_IOT_H
