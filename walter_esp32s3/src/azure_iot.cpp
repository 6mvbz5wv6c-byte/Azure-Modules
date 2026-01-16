/**
 * @file azure_iot.cpp
 * @brief Azure IoT Hub client implementation for Walter ESP32-S3
 *
 * Implements MQTTS connection to Azure IoT Hub via LTE modem with
 * SAS token authentication and Base64-encoded telemetry payloads.
 */

#include "azure_iot.h"
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>

// DigiCert Global Root G2 certificate for Azure IoT Hub
// This certificate is required for TLS connection to Azure
static const char AZURE_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMCcit2BnLI72A2i+iRGPqRLu4T7G0kC6Wm5vS7R0rRfA7kCFnzN5cSb6LEk
gHDBJ1qWc5U4t+CGjl0fZYuOKE5p+M9LcLH9pZJqkRWPBVGnWJZC2Z8dKMC7eOZr
Lr6tBv/fcBNqy/SggkLVcFEq0u8c7lUE8VjlJ93G8TTMKcUC7GPNlLAKa5P54P3h
P6yuqC3Z/ufJHgkGVN92EZNjJ/KShBv9wpn1Q8bABvqRmqIahuJk8WVzgV/lkVHi
Ls/J0lHNw4C4BVlR/qm8cjR4RNzksJLXwPJNif7DomyH4DSmPSnjC6G4X/6ze37m
OdQ=
-----END CERTIFICATE-----
)EOF";

// =============================================================================
// CONSTRUCTOR
// =============================================================================

AzureIoTClient::AzureIoTClient(WalterModem& modem)
    : _modem(modem)
    , _connected(false)
    , _lteConnected(false)
    , _sasExpiry(0)
    , _publishCount(0)
    , _errorCount(0)
    , _rssi(-999)
    , _telemetryTask(nullptr)
    , _ringBuffer(nullptr)
    , _stopRequested(false)
    , _payloadBuffer(nullptr)
{
    memset(_sasToken, 0, sizeof(_sasToken));
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool AzureIoTClient::begin() {
    LOG_PRINTLN("[Azure] Initializing Azure IoT Hub client");

    // Allocate payload buffer from PSRAM if available
    _payloadBuffer = (char*)ps_malloc(PAYLOAD_BUFFER_SIZE);
    if (!_payloadBuffer) {
        _payloadBuffer = (char*)malloc(PAYLOAD_BUFFER_SIZE);
    }

    if (!_payloadBuffer) {
        LOG_PRINTLN("[Azure] ERROR: Failed to allocate payload buffer");
        return false;
    }

    LOG_PRINTF("[Azure] Payload buffer allocated: %d bytes\n", PAYLOAD_BUFFER_SIZE);
    return true;
}

// =============================================================================
// CONNECTION
// =============================================================================

bool AzureIoTClient::connect() {
    LOG_PRINTLN("[Azure] Connecting to Azure IoT Hub...");

    // Step 1: Configure LTE modem APN
    LOG_PRINTF("[Azure] Setting APN: %s\n", LTE_APN);

    if (!_modem.definePDPContext(LTE_APN, WALTER_MODEM_PDP_AUTH_PROTO_NONE)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to define PDP context");
        return false;
    }

    // Step 2: Set modem to full operational state
    if (!_modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to set operational state");
        return false;
    }

    // Step 3: Wait for network registration
    LOG_PRINTLN("[Azure] Waiting for LTE network...");
    WalterModemNetworkRegState regState = _modem.getNetworkRegState();
    int attempts = 0;
    const int maxAttempts = 60;  // 30 seconds

    while (regState != WALTER_MODEM_NETWORK_REG_REGISTERED_HOME &&
           regState != WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
        delay(500);
        regState = _modem.getNetworkRegState();
        attempts++;

        if (attempts >= maxAttempts) {
            LOG_PRINTLN("[Azure] ERROR: Network registration timeout");
            return false;
        }

        if (attempts % 10 == 0) {
            LOG_PRINTF("[Azure] Still waiting for network... (%d/%d)\n", attempts, maxAttempts);
        }
    }

    _lteConnected = true;
    LOG_PRINTLN("[Azure] LTE network connected!");

    // Step 4: Generate SAS token
    uint32_t now = time(nullptr);
    if (now < 1000000000) {
        // Time not set, use modem time or estimate
        now = 1704067200;  // Jan 1, 2024 as fallback
    }

    uint32_t expiry = now + (AZURE_SAS_TTL_HOURS * 3600);

    // Resource URI: hostname/devices/deviceId
    char resourceUri[256];
    snprintf(resourceUri, sizeof(resourceUri), "%s/devices/%s",
             AZURE_IOT_HUB_HOST, AZURE_DEVICE_ID);

    if (!generateSasToken(resourceUri, AZURE_SAS_KEY, expiry, _sasToken, sizeof(_sasToken))) {
        LOG_PRINTLN("[Azure] ERROR: Failed to generate SAS token");
        return false;
    }

    _sasExpiry = expiry;
    LOG_PRINTF("[Azure] SAS token generated, expires: %u\n", expiry);

    // Step 5: Configure TLS with Azure root CA
    // Upload certificate to modem NVS at index 0
    const uint8_t TLS_CERT_INDEX = 0;
    const uint8_t TLS_PROFILE_ID = 1;

    if (!_modem.tlsWriteCredential(false, TLS_CERT_INDEX, AZURE_ROOT_CA)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to write TLS certificate");
        _errorCount++;
        return false;
    }

    // Configure TLS profile with CA validation
    if (!_modem.tlsConfigProfile(TLS_PROFILE_ID, WALTER_MODEM_TLS_VALIDATION_CA,
                                  WALTER_MODEM_TLS_VERSION_12, TLS_CERT_INDEX)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to configure TLS profile");
        _errorCount++;
        return false;
    }

    // Step 6: Connect MQTT to Azure IoT Hub
    // Username format: {iothubhostname}/{device-id}/?api-version=2021-04-12
    char mqttUsername[256];
    snprintf(mqttUsername, sizeof(mqttUsername), "%s/%s/?api-version=2021-04-12",
             AZURE_IOT_HUB_HOST, AZURE_DEVICE_ID);

    LOG_PRINTF("[Azure] MQTT connecting to %s:%d\n", AZURE_IOT_HUB_HOST, AZURE_MQTT_PORT);
    LOG_PRINTF("[Azure] Client ID: %s\n", AZURE_DEVICE_ID);

    // Configure MQTT client with credentials and TLS profile
    if (!_modem.mqttConfig(AZURE_DEVICE_ID, mqttUsername, _sasToken, TLS_PROFILE_ID)) {
        LOG_PRINTLN("[Azure] ERROR: MQTT config failed");
        _errorCount++;
        return false;
    }

    // Connect to MQTT broker (credentials already configured via mqttConfig)
    if (!_modem.mqttConnect(AZURE_IOT_HUB_HOST, AZURE_MQTT_PORT)) {
        LOG_PRINTLN("[Azure] ERROR: MQTT connection failed");
        _errorCount++;
        return false;
    }

    _connected = true;
    LOG_PRINTLN("[Azure] Connected to Azure IoT Hub!");

    return true;
}

void AzureIoTClient::disconnect() {
    if (_connected) {
        _modem.mqttDisconnect();
        _connected = false;
        LOG_PRINTLN("[Azure] Disconnected from Azure IoT Hub");
    }
}

// =============================================================================
// TELEMETRY PUBLISHING
// =============================================================================

bool AzureIoTClient::publishFrame(const AdcFrame& frame) {
    if (!_connected) {
        LOG_PRINTLN("[Azure] Not connected, cannot publish");
        _errorCount++;
        return false;
    }

    // Check if SAS token needs refresh
    uint32_t now = time(nullptr);
    if (now > 1000000000 && now > (_sasExpiry - 300)) {  // Refresh 5 min before expiry
        LOG_PRINTLN("[Azure] SAS token expiring, reconnecting...");
        disconnect();
        if (!connect()) {
            return false;
        }
    }

    // Build JSON payload
    // Format: {"deviceId":"...", "startUs":..., "endUs":..., "samplesB64":"..."}

    // Base64 encode samples (int32 LE)
    size_t samplesBytes = frame.numSamples * sizeof(int32_t);
    size_t base64Len = ((samplesBytes + 2) / 3) * 4 + 1;

    char* base64Samples = _payloadBuffer;
    base64Encode((uint8_t*)frame.samples, samplesBytes, base64Samples, base64Len);

    // Build JSON
    char* jsonPayload = _payloadBuffer + base64Len + 16;  // Leave space for base64
    size_t jsonLen = snprintf(jsonPayload, PAYLOAD_BUFFER_SIZE - base64Len - 16,
        "{"
        "\"deviceId\":\"%s\","
        "\"frameIndex\":%u,"
        "\"startUs\":%lld,"
        "\"endUs\":%lld,"
        "\"sampleRate\":%.1f,"
        "\"numSamples\":%u,"
        "\"samplesB64\":\"%s\""
        "}",
        AZURE_DEVICE_ID,
        frame.frameIndex,
        (long long)frame.startTimeUs,
        (long long)frame.endTimeUs,
        frame.sampleRateHz,
        frame.numSamples,
        base64Samples
    );

    // Publish to Azure IoT Hub telemetry topic
    if (_modem.mqttPublish(AZURE_TELEMETRY_TOPIC, (uint8_t*)jsonPayload, jsonLen)) {
        _publishCount++;
        return true;
    } else {
        LOG_PRINTLN("[Azure] MQTT publish failed");
        _errorCount++;
        _connected = false;  // Mark as disconnected for reconnection
        return false;
    }
}

// =============================================================================
// TELEMETRY TASK
// =============================================================================

void AzureIoTClient::startTelemetryTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle) {
    _ringBuffer = ringBuffer;
    _stopRequested = false;

    xTaskCreatePinnedToCore(
        telemetryTaskFunc,
        "Azure_Telem",
        TASK_STACK_TELEMETRY,
        this,
        TASK_PRIORITY_TELEMETRY,
        &_telemetryTask,
        TASK_CORE_TELEMETRY
    );

    if (taskHandle) {
        *taskHandle = _telemetryTask;
    }

    LOG_PRINTLN("[Azure] Telemetry task started");
}

void AzureIoTClient::stopTelemetryTask() {
    _stopRequested = true;

    if (_telemetryTask) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _telemetryTask = nullptr;
    }

    disconnect();
    LOG_PRINTLN("[Azure] Telemetry task stopped");
}

void AzureIoTClient::telemetryTaskFunc(void* param) {
    AzureIoTClient* client = static_cast<AzureIoTClient*>(param);

    LOG_PRINTLN("[Azure Task] Starting telemetry loop");

    // Create buffer cursor
    BufferCursor cursor(*client->_ringBuffer);
    cursor.reset();  // Start from current position

    // Connection retry state
    uint32_t retryDelay = 2000;
    const uint32_t maxRetryDelay = 60000;

    while (!client->_stopRequested) {
        // Ensure connection
        if (!client->_connected) {
            LOG_PRINTLN("[Azure Task] Attempting connection...");

            if (client->connect()) {
                retryDelay = 2000;  // Reset retry delay on success
            } else {
                LOG_PRINTF("[Azure Task] Connection failed, retry in %u ms\n", retryDelay);
                vTaskDelay(pdMS_TO_TICKS(retryDelay));
                retryDelay = min(retryDelay * 2, maxRetryDelay);
                continue;
            }
        }

        // Wait for and process frames
        AdcFrame frame;
        if (cursor.waitAndRead(frame, 1000)) {
            if (!client->publishFrame(frame)) {
                LOG_PRINTLN("[Azure Task] Publish failed, will reconnect");
                // Frame will be lost if buffer overflows during reconnection
            }
        }

        // Yield to other tasks
        taskYIELD();
    }

    LOG_PRINTLN("[Azure Task] Telemetry loop exited");
    vTaskDelete(nullptr);
}

// =============================================================================
// SAS TOKEN GENERATION
// =============================================================================

bool AzureIoTClient::generateSasToken(const char* resourceUri, const char* key,
                                       uint32_t expiryTime, char* output, size_t outputLen) {
    // Decode base64 key
    uint8_t keyDecoded[64];
    size_t keyDecodedLen = base64Decode(key, strlen(key), keyDecoded, sizeof(keyDecoded));
    if (keyDecodedLen == 0) {
        LOG_PRINTLN("[Azure] Failed to decode SAS key");
        return false;
    }

    // URL encode resource URI
    char encodedUri[256];
    urlEncode(resourceUri, encodedUri, sizeof(encodedUri));

    // Build string to sign: {URL-encoded-resourceURI}\n{expiry}
    char stringToSign[512];
    snprintf(stringToSign, sizeof(stringToSign), "%s\n%u", encodedUri, expiryTime);

    // Compute HMAC-SHA256
    uint8_t signature[32];
    if (!hmacSha256(keyDecoded, keyDecodedLen,
                    (uint8_t*)stringToSign, strlen(stringToSign),
                    signature)) {
        LOG_PRINTLN("[Azure] HMAC-SHA256 failed");
        return false;
    }

    // Base64 encode signature
    char signatureB64[64];
    base64Encode(signature, 32, signatureB64, sizeof(signatureB64));

    // URL encode signature
    char signatureEncoded[128];
    urlEncode(signatureB64, signatureEncoded, sizeof(signatureEncoded));

    // Build SAS token
    // SharedAccessSignature sr={resourceUri}&sig={signature}&se={expiry}
    snprintf(output, outputLen,
             "SharedAccessSignature sr=%s&sig=%s&se=%u",
             encodedUri, signatureEncoded, expiryTime);

    return true;
}

bool AzureIoTClient::hmacSha256(const uint8_t* key, size_t keyLen,
                                 const uint8_t* data, size_t dataLen,
                                 uint8_t* output) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr) {
        mbedtls_md_free(&ctx);
        return false;
    }

    if (mbedtls_md_setup(&ctx, mdInfo, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    if (mbedtls_md_hmac_starts(&ctx, key, keyLen) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    if (mbedtls_md_hmac_update(&ctx, data, dataLen) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    if (mbedtls_md_hmac_finish(&ctx, output) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    mbedtls_md_free(&ctx);
    return true;
}

size_t AzureIoTClient::base64Encode(const uint8_t* input, size_t inputLen,
                                     char* output, size_t outputLen) {
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char*)output, outputLen, &olen, input, inputLen);
    if (ret != 0) {
        return 0;
    }
    output[olen] = '\0';
    return olen;
}

size_t AzureIoTClient::base64Decode(const char* input, size_t inputLen,
                                     uint8_t* output, size_t outputLen) {
    size_t olen = 0;
    int ret = mbedtls_base64_decode(output, outputLen, &olen, (unsigned char*)input, inputLen);
    if (ret != 0) {
        return 0;
    }
    return olen;
}

void AzureIoTClient::urlEncode(const char* input, char* output, size_t outputLen) {
    size_t outIdx = 0;

    for (size_t i = 0; input[i] && outIdx < outputLen - 4; i++) {
        char c = input[i];

        // Characters that don't need encoding
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            output[outIdx++] = c;
        } else {
            // Percent-encode
            output[outIdx++] = '%';
            output[outIdx++] = "0123456789ABCDEF"[(c >> 4) & 0x0F];
            output[outIdx++] = "0123456789ABCDEF"[c & 0x0F];
        }
    }

    output[outIdx] = '\0';
}
