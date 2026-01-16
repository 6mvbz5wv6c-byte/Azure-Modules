/**
 * @file azure_iot.cpp
 * @brief Azure IoT Hub client implementation for Walter ESP32-S3
 *
 * Implements MQTTS connection to Azure IoT Hub via LTE modem with
 * SAS token authentication and Base64-encoded telemetry payloads.
 */

#include "azure_iot.h"
#include "gnss.h"
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>

// DigiCert Global Root G2 certificate for Azure IoT Hub
// This is the primary certificate after Azure's September 2024 migration
// See: https://learn.microsoft.com/en-us/azure/iot-hub/migrate-tls-certificate
static const char AZURE_ROOT_CA_DIGICERT[] PROGMEM = R"EOF(
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

// Microsoft RSA Root Certificate Authority 2017 (backup)
// Recommended by Azure as fallback if DigiCert G2 is retired
static const char AZURE_ROOT_CA_MSFT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFqDCCA5CgAwIBAgIQHtOXCV/YtLNHcB6qvn9FszANBgkqhkiG9w0BAQwFAMDh
MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMTIw
MAYDVQQDEylNaWNyb3NvZnQgUlNBIFJvb3QgQ2VydGlmaWNhdGUgQXV0aG9yaXR5
IDIwMTcwHhcNMTkxMjE4MjI1MTIyWhcNNDIwNzE4MjMwMDIzWjBhMQswCQYDVQQG
EwJVUzEeMBwGA1UEChMVTWljcm9zb2Z0IENvcnBvcmF0aW9uMTIwMAYDVQQDEylN
aWNyb3NvZnQgUlNBIFJvb3QgQ2VydGlmaWNhdGUgQXV0aG9yaXR5IDIwMTcwggIi
MA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQDKyvTp8YFFnqfMDRUmO/D1GPpH
Qx36oKf+0l6dH/M0NxLqIiG8UHdoW+KZnP1wCE+5Q5ybYjXfp3P5xA9WpcaJ2E01
FNQeZrcQD8BRF1SFiJcPPkE3jj3NvC9fA6Mfr9bK5dMJWwGVPdI67R7QFmRpE2lY
TdgGaXZLZ1qjjIJ8dGj91YxDkwbkbjjnRo5eXVdgPpSu0gVQAY1HGjwmaNxlLHYf
nv3j/0QNYLYmqAg6DsBq0y0SBk0AYOlZSTpz6PQc5oy2SMQV6LfC8lKMPvmQBNOx
6IhELY1mIGJQFwkPnqzPPHiHpNP1N+47PN2YZi0Y9DAwc7h0hF3Dfg1rbYf9P6Wb
Y/q3MzOzCvAhKpfQ1C5M7XsE0mC5qdF8Ujhx5McYPzuX3XxDhdkEuV3FVjlmkPfA
0BvDAMFXs1y/DDSW2c1HGcHlDFW0o1FD0+kGbPBLW63Gv+5GyLmS7xKLMNE2RDNN
G3Gt4Vns6YGn1gLDBeZuH3uy1ZhzAw7KLGIqMmLC3NrhKWIiGPcGwsFYSGpPY8N7
F3HFPXZDRK14i0hJLR1NfXs3b0GI1ocTJQCZj3F5WwP0JVPuD8zjYuG+wlhlslsB
PN0VIEiG3WKW+oSywJLYRFC3umXBK8t0hNJz8enLBCp4Mfq4TlDYNe29xYhJ+q3r
NPBVAB8CJ/6GXOsNwwIDAQABo1QwUjAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/
BAUwAwEB/zAdBgNVHQ4EFgQUCctZf4aycI8awznjwNnpv7tNsiMwEAYJKwYBBAGC
NxUBBAMCAQAwDQYJKoZIhvcNAQEMBQADggIBAKyvPl3CEZaJF+J2lDHaLphxh0FN
TqwbXxKv4fMRvYO2D0FLj9QW3xpS5NkKOXqD0/2r2nk0MDM7s0vO7gfrdRDCQU4h
EUwxYVzXt7Q5SDVk+jKTBucaZ2h/HIT1k3yUkb+nTnRY+hJdj8hVxMZ9E3M9PqPj
fjEKEKNe7c8DoEbB9x29W7w7w5iEKE6fG+HL7pT7waRAM6LZv6RLdyXJ5wOY1K3g
uyBxRzJYLgDK3D4LUk+bJoQALEqx7TRVW4ElIa7DG2jWHKJzPxYag7ikshE8PICD
ZEG2fZw+Cr8jWDFDDv0j/3V1BN9OI0IK5+DCNlLEXKGJm5YQ3s+nWVfaIiQ7FTIM
rh2BLAcP5unn6iY0MI49OakN7zEA7SzzMSECf4P6Bo5sO0mLyMWq9P+Q3yDREnnf
V0NTJI1j+XPloM4ohCfrWp0nMq/4FM+xNwA8dSSNAJjJXVtKPF7m4g7vWPXmHcY6
cGmzn7oz1JPMQJK7bI1QKBF+pPbuXFsc9R7i1p3EzHHo0aO3P7c2a3A2DmoMNP84
6qRFz0MVsLnadGc/kYDq+m02xe4S8bFSzPRbyeMwPNF5SB/5W+lFrWlC1xERVB3B
PLJVi+HzfgfzmWE8szY2kaRp3T/1BzOiQ8bnE7BOAUj3p2OT0y3U1zAFvD4q3LW9
njk47oFBJl4RyJbz
-----END CERTIFICATE-----
)EOF";

// =============================================================================
// CONSTRUCTOR
// =============================================================================

AzureIoTClient::AzureIoTClient()
    : _connected(false)
    , _lteConnected(false)
    , _sasExpiry(0)
    , _publishCount(0)
    , _errorCount(0)
    , _rssi(-999)
    , _telemetryTask(nullptr)
    , _ringBuffer(nullptr)
    , _stopRequested(false)
    , _adcAvailable(false)
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

    // Check if LTE is already connected - skip modem init if so
    if (_lteConnected) {
        WalterModemNetworkRegState regState = WalterModem::getNetworkRegState();
        if (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
            regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
            LOG_PRINTLN("[Azure] LTE already connected, skipping modem init");
        } else {
            LOG_PRINTLN("[Azure] LTE was connected but lost, re-initializing...");
            _lteConnected = false;
        }
    }

    // Only do full modem init if not already connected
    if (!_lteConnected) {
        // Step 1: Set modem to NO_RF state (required before configuring PDP context)
        LOG_PRINTLN("[Azure] Setting modem to NO_RF state...");
        if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
            LOG_PRINTLN("[Azure] ERROR: Failed to set NO_RF state");
            return false;
        }

        // Step 2: Define PDP context with APN
        LOG_PRINTF("[Azure] Defining PDP context with APN: %s\n", LTE_APN);
        if (!WalterModem::definePDPContext(1, LTE_APN)) {
            LOG_PRINTLN("[Azure] ERROR: Failed to define PDP context");
            return false;
        }
        LOG_PRINTLN("[Azure] PDP context defined");

        // Step 3: Set modem to full operational state
        LOG_PRINTLN("[Azure] Setting modem to FULL state...");
        if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_FULL)) {
            LOG_PRINTLN("[Azure] ERROR: Failed to set operational state");
            return false;
        }

        // Step 4: Configure network selection mode
        LOG_PRINTLN("[Azure] Setting automatic network selection...");
        WalterModem::setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC);

        // Step 5: Wait for network registration
        LOG_PRINTLN("[Azure] Waiting for LTE network...");
        WalterModemNetworkRegState regState = WalterModem::getNetworkRegState();
        int attempts = 0;
        const int maxAttempts = 60;  // 30 seconds

        while (regState != WALTER_MODEM_NETWORK_REG_REGISTERED_HOME &&
               regState != WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
            delay(500);
            regState = WalterModem::getNetworkRegState();
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
    }

    // Step 6: Generate SAS token
    // Note: NTP doesn't work over LTE (only WiFi), so we use a hardcoded timestamp
    // Azure accepts tokens with expiry up to 365 days in the future
    LOG_PRINTLN("[Azure] ========== TIMESTAMP SETUP ==========");
    uint32_t now = time(nullptr);
    LOG_PRINTF("[Azure] System time from time(): %lu\n", (unsigned long)now);

    // Check if time is reasonable (after year 2020)
    // 1577836800 = Jan 1, 2020 00:00:00 UTC
    if (now < 1577836800) {
        // Time not set - use a recent timestamp for this deployment
        // Jan 16, 2026 00:00:00 UTC = 1736985600
        now = 1736985600;
        LOG_PRINTLN("[Azure] Time not set, using hardcoded timestamp");
        LOG_PRINTLN("[Azure] Hardcoded: Jan 16, 2026 00:00:00 UTC");
    } else {
        LOG_PRINTLN("[Azure] Using system time (appears valid)");
    }

    LOG_PRINTF("[Azure] Current timestamp (used): %lu\n", (unsigned long)now);

    // Calculate expiry - Azure allows up to 365 days, we use 24 hours default
    uint32_t expiry = now + (AZURE_SAS_TTL_HOURS * 3600);
    LOG_PRINTF("[Azure] Token TTL: %d hours\n", AZURE_SAS_TTL_HOURS);
    LOG_PRINTF("[Azure] Token expiry timestamp: %lu\n", (unsigned long)expiry);
    LOG_PRINTLN("[Azure] ======================================");

    // Resource URI: hostname/devices/deviceId
    char resourceUri[256];
    snprintf(resourceUri, sizeof(resourceUri), "%s/devices/%s",
             AZURE_IOT_HUB_HOST, AZURE_DEVICE_ID);

    LOG_PRINTF("[Azure] Generating SAS token for: %s\n", resourceUri);

    if (!generateSasToken(resourceUri, AZURE_SAS_KEY, expiry, _sasToken, sizeof(_sasToken))) {
        LOG_PRINTLN("[Azure] ERROR: Failed to generate SAS token");
        return false;
    }

    _sasExpiry = expiry;
    LOG_PRINTF("[Azure] SAS token generated, expires: %u (in %u hours)\n", expiry, AZURE_SAS_TTL_HOURS);

    // Step 7: Configure TLS with Azure root CA certificates
    // Note: Certificate indexes 0-10 are RESERVED for Sequans/BlueCherry
    // Use index 12 or higher for user certificates
    const uint8_t TLS_CERT_INDEX_DIGICERT = 12;
    const uint8_t TLS_CERT_INDEX_MSFT = 13;
    const uint8_t TLS_PROFILE_ID = 2;  // Profile 2 for user MQTTS

    // Write DigiCert Global Root G2 (primary)
    LOG_PRINTLN("[Azure] Writing DigiCert G2 root certificate to modem...");
    if (!WalterModem::tlsWriteCredential(false, TLS_CERT_INDEX_DIGICERT, AZURE_ROOT_CA_DIGICERT)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to write DigiCert certificate");
        _errorCount++;
        return false;
    }
    LOG_PRINTLN("[Azure] DigiCert G2 certificate written OK");

    // Write Microsoft RSA Root CA 2017 (backup)
    LOG_PRINTLN("[Azure] Writing Microsoft RSA root certificate to modem...");
    if (!WalterModem::tlsWriteCredential(false, TLS_CERT_INDEX_MSFT, AZURE_ROOT_CA_MSFT)) {
        LOG_PRINTLN("[Azure] WARNING: Failed to write Microsoft RSA certificate (continuing with DigiCert only)");
        // Don't fail - DigiCert should be sufficient
    } else {
        LOG_PRINTLN("[Azure] Microsoft RSA certificate written OK");
    }

    // Configure TLS profile with CA validation (use DigiCert as primary)
    LOG_PRINTF("[Azure] Configuring TLS profile %d with cert index %d...\n", TLS_PROFILE_ID, TLS_CERT_INDEX_DIGICERT);
    if (!WalterModem::tlsConfigProfile(TLS_PROFILE_ID, WALTER_MODEM_TLS_VALIDATION_CA,
                                  WALTER_MODEM_TLS_VERSION_12, TLS_CERT_INDEX_DIGICERT)) {
        LOG_PRINTLN("[Azure] ERROR: Failed to configure TLS profile");
        _errorCount++;
        return false;
    }
    LOG_PRINTLN("[Azure] TLS profile configured OK");

    // Step 8: Connect MQTT to Azure IoT Hub
    // Username format: {iothubhostname}/{device-id}/?api-version=2021-04-12
    char mqttUsername[256];
    snprintf(mqttUsername, sizeof(mqttUsername), "%s/%s/?api-version=2021-04-12",
             AZURE_IOT_HUB_HOST, AZURE_DEVICE_ID);

    LOG_PRINTLN("[Azure] ========== MQTT CONNECTION DEBUG ==========");
    LOG_PRINTF("[Azure] MQTT broker: %s:%d\n", AZURE_IOT_HUB_HOST, AZURE_MQTT_PORT);
    LOG_PRINTF("[Azure] MQTT client ID: %s\n", AZURE_DEVICE_ID);
    LOG_PRINTF("[Azure] MQTT username: %s\n", mqttUsername);
    LOG_PRINTF("[Azure] MQTT username length: %d\n", strlen(mqttUsername));
    LOG_PRINTF("[Azure] SAS token length: %d\n", strlen(_sasToken));

    // Print SAS token in chunks for debugging (long strings may be truncated in serial)
    LOG_PRINTLN("[Azure] SAS token (chunked):");
    const char* p = _sasToken;
    int chunk = 0;
    while (*p) {
        char buf[81];
        strncpy(buf, p, 80);
        buf[80] = '\0';
        LOG_PRINTF("[Azure] [%d]: %s\n", chunk++, buf);
        p += strlen(buf);
    }
    LOG_PRINTLN("[Azure] ============================================");

    // Configure MQTT client with credentials and TLS profile
    LOG_PRINTLN("[Azure] Step 8a: Configuring MQTT client...");
    LOG_PRINTF("[Azure] mqttConfig(clientId=%s, username=<len:%d>, password=<len:%d>, tlsProfile=%d)\n",
               AZURE_DEVICE_ID, strlen(mqttUsername), strlen(_sasToken), TLS_PROFILE_ID);

    if (!WalterModem::mqttConfig(AZURE_DEVICE_ID, mqttUsername, _sasToken, TLS_PROFILE_ID)) {
        LOG_PRINTLN("[Azure] ERROR: MQTT config failed");
        LOG_PRINTLN("[Azure] This could mean:");
        LOG_PRINTLN("[Azure]   - Modem rejected credentials format");
        LOG_PRINTLN("[Azure]   - Buffer overflow (username/password too long)");
        LOG_PRINTLN("[Azure]   - TLS profile not configured correctly");
        _errorCount++;
        return false;
    }
    LOG_PRINTLN("[Azure] MQTT client configured OK");

    // Connect to MQTT broker
    LOG_PRINTLN("[Azure] Step 8b: Connecting to MQTT broker...");
    LOG_PRINTF("[Azure] mqttConnect(%s, %d)\n", AZURE_IOT_HUB_HOST, AZURE_MQTT_PORT);

    if (!WalterModem::mqttConnect(AZURE_IOT_HUB_HOST, AZURE_MQTT_PORT)) {
        LOG_PRINTLN("[Azure] ERROR: MQTT connection failed!");
        LOG_PRINTLN("[Azure] Possible causes:");
        LOG_PRINTLN("[Azure]   1. TLS handshake failure (wrong certificate)");
        LOG_PRINTLN("[Azure]   2. Authentication rejected (invalid SAS token)");
        LOG_PRINTLN("[Azure]   3. Network issue (DNS, firewall, connectivity)");
        LOG_PRINTLN("[Azure]   4. Azure IoT Hub rejected connection");
        LOG_PRINTLN("[Azure]   5. SAS token expired (check timestamp)");
        _errorCount++;
        return false;
    }

    _connected = true;
    LOG_PRINTLN("[Azure] Connected to Azure IoT Hub!");

    return true;
}

void AzureIoTClient::disconnect() {
    if (_connected) {
        WalterModem::mqttDisconnect();
        _connected = false;
        // Note: Don't reset _lteConnected - LTE may still be active
        LOG_PRINTLN("[Azure] Disconnected from MQTT (LTE still active)");
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

    // Build GNSS JSON fragment
    char gnssJson[256];
    gnssManager.toJsonFragment(gnssJson, sizeof(gnssJson));

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
        "\"samplesB64\":\"%s\","
        "%s"
        "}",
        AZURE_DEVICE_ID,
        frame.frameIndex,
        (long long)frame.startTimeUs,
        (long long)frame.endTimeUs,
        frame.sampleRateHz,
        frame.numSamples,
        base64Samples,
        gnssJson
    );

    // Publish to Azure IoT Hub telemetry topic
    if (WalterModem::mqttPublish(AZURE_TELEMETRY_TOPIC, (uint8_t*)jsonPayload, jsonLen)) {
        _publishCount++;
        return true;
    } else {
        LOG_PRINTLN("[Azure] MQTT publish failed");
        _errorCount++;
        _connected = false;  // Mark as disconnected for reconnection
        return false;
    }
}

bool AzureIoTClient::publishStatus(bool adcOnline, const char* errorMsg) {
    if (!_connected) {
        LOG_PRINTLN("[Azure] Not connected, cannot publish status");
        _errorCount++;
        return false;
    }

    // Check if SAS token needs refresh
    uint32_t now = time(nullptr);
    if (now > 1000000000 && now > (_sasExpiry - 300)) {
        LOG_PRINTLN("[Azure] SAS token expiring, reconnecting...");
        disconnect();
        if (!connect()) {
            return false;
        }
    }

    // Build GNSS JSON fragment
    char gnssJson[256];
    gnssManager.toJsonFragment(gnssJson, sizeof(gnssJson));

    // Build status JSON payload
    char* jsonPayload = _payloadBuffer;
    size_t jsonLen = snprintf(jsonPayload, PAYLOAD_BUFFER_SIZE,
        "{"
        "\"deviceId\":\"%s\","
        "\"messageType\":\"status\","
        "\"timestamp\":%lu,"
        "\"uptimeMs\":%lu,"
        "\"adcOnline\":%s,"
        "\"lteRssi\":%d,"
        "\"freeHeap\":%u,"
        "\"publishCount\":%u,"
        "\"errorCount\":%u,"
        "%s"
        "%s%s%s"
        "}",
        AZURE_DEVICE_ID,
        (unsigned long)now,
        (unsigned long)millis(),
        adcOnline ? "true" : "false",
        _rssi,
        ESP.getFreeHeap(),
        _publishCount,
        _errorCount,
        gnssJson,
        errorMsg ? ",\"error\":\"" : "",
        errorMsg ? errorMsg : "",
        errorMsg ? "\"" : ""
    );

    LOG_PRINTF("[Azure] Publishing status: adcOnline=%s\n", adcOnline ? "true" : "false");

    // Publish to Azure IoT Hub telemetry topic
    if (WalterModem::mqttPublish(AZURE_TELEMETRY_TOPIC, (uint8_t*)jsonPayload, jsonLen)) {
        _publishCount++;
        return true;
    } else {
        LOG_PRINTLN("[Azure] MQTT status publish failed");
        _errorCount++;
        _connected = false;
        return false;
    }
}

// =============================================================================
// TELEMETRY TASK
// =============================================================================

void AzureIoTClient::startTelemetryTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle, bool adcAvailable) {
    _ringBuffer = ringBuffer;
    _stopRequested = false;
    _adcAvailable = adcAvailable;

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

    LOG_PRINTF("[Azure] Telemetry task started (ADC %s)\n", adcAvailable ? "available" : "unavailable");
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

    // Create buffer cursor only if ADC available
    BufferCursor* cursor = nullptr;
    if (client->_adcAvailable && client->_ringBuffer) {
        cursor = new BufferCursor(*client->_ringBuffer);
        cursor->reset();
    }

    // Connection retry state
    uint32_t retryDelay = 2000;
    const uint32_t maxRetryDelay = 60000;

    // Status heartbeat timing
    uint32_t lastStatusTime = 0;
    const uint32_t statusIntervalMs = 30000;  // Send status every 30 seconds

    while (!client->_stopRequested) {
        // Ensure connection
        if (!client->_connected) {
            LOG_PRINTLN("[Azure Task] Attempting connection...");

            if (client->connect()) {
                retryDelay = 2000;  // Reset retry delay on success
                // Send immediate status on connect
                client->publishStatus(client->_adcAvailable,
                    client->_adcAvailable ? nullptr : "ADC not detected");
                lastStatusTime = millis();
            } else {
                LOG_PRINTF("[Azure Task] Connection failed, retry in %u ms\n", retryDelay);
                vTaskDelay(pdMS_TO_TICKS(retryDelay));
                retryDelay = min(retryDelay * 2, maxRetryDelay);
                continue;
            }
        }

        // Send periodic status heartbeat
        uint32_t now = millis();
        if (now - lastStatusTime >= statusIntervalMs) {
            client->publishStatus(client->_adcAvailable,
                client->_adcAvailable ? nullptr : "ADC not detected");
            lastStatusTime = now;
        }

#if GNSS_UPDATE_INTERVAL_MS > 0
        // Check if GNSS location needs update
        if (gnssManager.needsUpdate()) {
            LOG_PRINTLN("[Azure Task] GNSS update needed, disconnecting MQTT...");

            // Disconnect MQTT (but remember we want to reconnect)
            client->disconnect();
            client->_lteConnected = false;  // Force full modem re-init after GNSS

            // Acquire new GNSS fix
            if (gnssManager.acquireFix()) {
                const GnssLocation& loc = gnssManager.getLocation();
                LOG_PRINTF("[Azure Task] GNSS updated: %.6f, %.6f\n",
                          loc.latitude, loc.longitude);
            } else {
                LOG_PRINTLN("[Azure Task] GNSS update failed");
            }

            // Reconnection will happen on next loop iteration
            continue;
        }
#endif

        // Process ADC frames if available
        if (cursor && client->_adcAvailable) {
            AdcFrame frame;
            if (cursor->waitAndRead(frame, 1000)) {
                if (!client->publishFrame(frame)) {
                    LOG_PRINTLN("[Azure Task] Publish failed, will reconnect");
                }
            }
        } else {
            // No ADC, just wait a bit before next status check
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Yield to other tasks
        taskYIELD();
    }

    if (cursor) {
        delete cursor;
    }

    LOG_PRINTLN("[Azure Task] Telemetry loop exited");
    vTaskDelete(nullptr);
}

// =============================================================================
// SAS TOKEN GENERATION
// =============================================================================

bool AzureIoTClient::generateSasToken(const char* resourceUri, const char* key,
                                       uint32_t expiryTime, char* output, size_t outputLen) {
    LOG_PRINTLN("[Azure] ========== SAS TOKEN GENERATION ==========");
    LOG_PRINTF("[Azure] Resource URI: %s\n", resourceUri);
    LOG_PRINTF("[Azure] Expiry time: %u\n", expiryTime);
    LOG_PRINTF("[Azure] Key length: %d\n", strlen(key));

    // Decode base64 key
    uint8_t keyDecoded[64];
    size_t keyDecodedLen = base64Decode(key, strlen(key), keyDecoded, sizeof(keyDecoded));
    if (keyDecodedLen == 0) {
        LOG_PRINTLN("[Azure] ERROR: Failed to decode SAS key from base64");
        return false;
    }
    LOG_PRINTF("[Azure] Decoded key length: %d bytes\n", keyDecodedLen);

    // URL encode resource URI
    char encodedUri[256];
    urlEncode(resourceUri, encodedUri, sizeof(encodedUri));
    LOG_PRINTF("[Azure] URL-encoded URI: %s\n", encodedUri);

    // Build string to sign: {URL-encoded-resourceURI}\n{expiry}
    char stringToSign[512];
    snprintf(stringToSign, sizeof(stringToSign), "%s\n%u", encodedUri, expiryTime);
    LOG_PRINTF("[Azure] String to sign: %s\\n%u\n", encodedUri, expiryTime);
    LOG_PRINTF("[Azure] String to sign length: %d\n", strlen(stringToSign));

    // Compute HMAC-SHA256
    uint8_t signature[32];
    if (!hmacSha256(keyDecoded, keyDecodedLen,
                    (uint8_t*)stringToSign, strlen(stringToSign),
                    signature)) {
        LOG_PRINTLN("[Azure] ERROR: HMAC-SHA256 computation failed");
        return false;
    }
    LOG_PRINTLN("[Azure] HMAC-SHA256 computed successfully");

    // Print first few bytes of signature for debugging
    LOG_PRINTF("[Azure] Signature (hex): %02x%02x%02x%02x...\n",
               signature[0], signature[1], signature[2], signature[3]);

    // Base64 encode signature
    char signatureB64[64];
    base64Encode(signature, 32, signatureB64, sizeof(signatureB64));
    LOG_PRINTF("[Azure] Base64 signature: %s\n", signatureB64);

    // URL encode signature
    char signatureEncoded[128];
    urlEncode(signatureB64, signatureEncoded, sizeof(signatureEncoded));
    LOG_PRINTF("[Azure] URL-encoded signature: %s\n", signatureEncoded);

    // Build SAS token
    // SharedAccessSignature sr={resourceUri}&sig={signature}&se={expiry}
    int tokenLen = snprintf(output, outputLen,
             "SharedAccessSignature sr=%s&sig=%s&se=%u",
             encodedUri, signatureEncoded, expiryTime);

    LOG_PRINTF("[Azure] SAS token generated, length: %d\n", tokenLen);
    LOG_PRINTLN("[Azure] ==========================================");

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
