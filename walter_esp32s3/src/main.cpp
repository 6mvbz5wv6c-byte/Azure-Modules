/**
 * @file main.cpp
 * @brief Geode AGM - Walter ESP32-S3 Main Application
 *
 * Combines ADS1256 high-speed ADC sampling with:
 * - Local WiFi AP web interface for real-time visualization
 * - Azure IoT Hub telemetry over LTE modem
 *
 * FreeRTOS Task Architecture:
 * - ADC Task (Core 1, Priority 5): Hardware interrupt-driven sampling at 1000 SPS
 * - WebUI Task (Core 0, Priority 2): WiFi AP + WebSocket streaming
 * - Telemetry Task (Core 0, Priority 3): Azure IoT Hub MQTT over LTE
 *
 * Hardware: DPTechnics Walter (ESP32-S3-WROOM-1-N16R2 + Sequans GM02SP LTE)
 */

#include <Arduino.h>
#include <SPI.h>
#include <WalterModem.h>

#include "config.h"
#include "ring_buffer.h"
#include "ads1256.h"
#include "azure_iot.h"
#include "webui.h"

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================

// SPI bus for ADS1256
// ESP32-S3 uses FSPI (not VSPI which is ESP32-only)
SPIClass adcSPI(FSPI);

// Core components
ADS1256         adc(adcSPI);
AdcRingBuffer   ringBuffer;
WalterModem     modem;
AzureIoTClient  azureClient(modem);
WebUIServer     webServer;

// Task handles
TaskHandle_t    adcTaskHandle = nullptr;
TaskHandle_t    telemetryTaskHandle = nullptr;
TaskHandle_t    webuiTaskHandle = nullptr;

// System state
volatile bool   systemRunning = true;
uint32_t        bootTime = 0;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

void printSystemInfo() {
    LOG_PRINTLN("\n========================================");
    LOG_PRINTLN("  Geode AGM - Walter ESP32-S3");
    LOG_PRINTLN("========================================");
    LOG_PRINTF("  CPU Freq:    %d MHz\n", getCpuFrequencyMhz());
    LOG_PRINTF("  Free Heap:   %d bytes\n", ESP.getFreeHeap());
    LOG_PRINTF("  Free PSRAM:  %d bytes\n", ESP.getFreePsram());
    LOG_PRINTF("  Flash Size:  %d MB\n", ESP.getFlashChipSize() / 1024 / 1024);
    LOG_PRINTF("  SDK Version: %s\n", ESP.getSdkVersion());
    LOG_PRINTLN("----------------------------------------");
    LOG_PRINTF("  Sample Rate: %d Hz\n", SAMPLE_RATE_HZ);
    LOG_PRINTF("  Frame Size:  %d samples\n", FRAME_SIZE);
    LOG_PRINTF("  WiFi SSID:   %s\n", WIFI_AP_SSID);
    LOG_PRINTF("  Azure Host:  %s\n", AZURE_IOT_HUB_HOST);
    LOG_PRINTF("  Device ID:   %s\n", AZURE_DEVICE_ID);
    LOG_PRINTLN("========================================\n");
}

void printStatus() {
    static uint32_t lastPrint = 0;
    uint32_t now = millis();

    if (now - lastPrint < 5000) return;
    lastPrint = now;

    uint32_t uptime = (now - bootTime) / 1000;
    uint32_t hours = uptime / 3600;
    uint32_t mins = (uptime % 3600) / 60;
    uint32_t secs = uptime % 60;

    LOG_PRINTF("\n[Status] Uptime: %02d:%02d:%02d | ", hours, mins, secs);
    LOG_PRINTF("Heap: %d | ", ESP.getFreeHeap());
    LOG_PRINTF("ADC Samples: %u | ", adc.getSampleCount());
    LOG_PRINTF("Frames: %u | ", ringBuffer.getFrameCount());
    LOG_PRINTF("Azure Pub: %u | ", azureClient.getPublishCount());
    LOG_PRINTF("WS Clients: %d\n", webServer.getClientCount());
}

// =============================================================================
// MODEM INITIALIZATION TASK
// =============================================================================

void modemInitTask(void* param) {
    LOG_PRINTLN("[Modem] Initializing Walter modem...");

    // Initialize modem (uses Serial2 internally)
    if (!WalterModem::begin(&Serial2)) {
        LOG_PRINTLN("[Modem] ERROR: Failed to initialize modem");
        LOG_PRINTLN("[Modem] Continuing without LTE connectivity...");
        vTaskDelete(nullptr);
        return;
    }

    LOG_PRINTLN("[Modem] Modem initialized successfully");

    // Initialize Azure IoT client
    if (azureClient.begin()) {
        // Start telemetry task
        azureClient.startTelemetryTask(&ringBuffer, &telemetryTaskHandle);
    } else {
        LOG_PRINTLN("[Modem] ERROR: Azure IoT client initialization failed");
    }

    vTaskDelete(nullptr);
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    // Initialize serial for debug output
    DEBUG_SERIAL.begin(DEBUG_BAUD);
    delay(2000);  // Wait for USB serial to initialize

    bootTime = millis();
    printSystemInfo();

    // Initialize ring buffer (creates FreeRTOS semaphores - must be after scheduler starts)
    LOG_PRINTLN("[Setup] Initializing ring buffer...");
    ringBuffer.begin();

    // Initialize SPI for ADS1256
    LOG_PRINTLN("[Setup] Initializing SPI...");
    adcSPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_ADS_CS);

    // Initialize ADS1256
    LOG_PRINTLN("[Setup] Initializing ADS1256...");
    if (!adc.begin()) {
        LOG_PRINTLN("[Setup] WARNING: ADS1256 init returned error, continuing anyway");
    }

    // Initialize Web UI server (WiFi AP)
    LOG_PRINTLN("[Setup] Initializing Web UI server...");
    if (webServer.begin()) {
        LOG_PRINTF("[Setup] Web UI available at http://%s\n", webServer.getIPAddress().toString().c_str());
    } else {
        LOG_PRINTLN("[Setup] ERROR: Web UI server failed to start");
    }

    // Start WebSocket streaming task
    webServer.startStreamingTask(&ringBuffer, &webuiTaskHandle);

    // Start ADC sampling task
    LOG_PRINTLN("[Setup] Starting ADC sampling task...");
    adc.startSamplingTask(&ringBuffer, &adcTaskHandle);

    // Start modem initialization in background task
    // This allows sampling and webui to start immediately while modem connects
    LOG_PRINTLN("[Setup] Starting modem initialization task...");
    xTaskCreatePinnedToCore(
        modemInitTask,
        "Modem_Init",
        TASK_STACK_MODEM,
        nullptr,
        TASK_PRIORITY_MODEM,
        nullptr,
        TASK_CORE_MODEM
    );

    LOG_PRINTLN("[Setup] Initialization complete!");
    LOG_PRINTLN("[Setup] Connect to WiFi: " WIFI_AP_SSID);
    LOG_PRINTF("[Setup] Open http://%s in browser\n", webServer.getIPAddress().toString().c_str());
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void loop() {
    // Print periodic status
    printStatus();

    // Main loop can be used for low-priority tasks
    // All critical work is done in FreeRTOS tasks

    // Monitor heap and PSRAM usage
    static uint32_t lastHeapCheck = 0;
    if (millis() - lastHeapCheck > 30000) {
        lastHeapCheck = millis();

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t freePsram = ESP.getFreePsram();

        if (freeHeap < 20000) {
            LOG_PRINTF("[Warning] Low heap memory: %d bytes\n", freeHeap);
        }
    }

    // Handle serial commands for debugging
    if (DEBUG_SERIAL.available()) {
        char cmd = DEBUG_SERIAL.read();
        switch (cmd) {
            case 's':
            case 'S':
                // Print status
                LOG_PRINTLN("\n=== Manual Status Request ===");
                LOG_PRINTF("ADC Sample Count: %u\n", adc.getSampleCount());
                LOG_PRINTF("ADC Dropped:      %u\n", adc.getDroppedCount());
                LOG_PRINTF("Ring Buffer Frames: %u\n", ringBuffer.getFrameCount());
                LOG_PRINTF("Ring Buffer Dropped: %u\n", ringBuffer.getDroppedFrames());
                LOG_PRINTF("Azure Connected:  %s\n", azureClient.isConnected() ? "Yes" : "No");
                LOG_PRINTF("Azure Published:  %u\n", azureClient.getPublishCount());
                LOG_PRINTF("Azure Errors:     %u\n", azureClient.getErrorCount());
                LOG_PRINTF("WebSocket Clients: %d\n", webServer.getClientCount());
                LOG_PRINTF("Free Heap:        %d\n", ESP.getFreeHeap());
                LOG_PRINTF("Free PSRAM:       %d\n", ESP.getFreePsram());
                break;

            case 'r':
            case 'R':
                // Restart
                LOG_PRINTLN("\n=== Restarting... ===");
                ESP.restart();
                break;

            case 'h':
            case 'H':
            case '?':
                // Help
                LOG_PRINTLN("\n=== Debug Commands ===");
                LOG_PRINTLN("  s - Print status");
                LOG_PRINTLN("  r - Restart device");
                LOG_PRINTLN("  h - Show this help");
                break;
        }
    }

    // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Note: Arduino framework provides app_main() automatically.
// Do not define it here - it causes multiple definition errors.
