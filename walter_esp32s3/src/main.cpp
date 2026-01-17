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
#include "gnss.h"

// External diagnostic function from ads1256_test.cpp
extern void runAdsDiagnostic();

// =============================================================================
// GLOBAL OBJECTS (pointers - allocated in setup() to avoid constructor issues)
// =============================================================================

// SPI bus for ADS1256
SPIClass* adcSPI = nullptr;

// Core components - allocated in setup() after Arduino/FreeRTOS is initialized
ADS1256*        adc = nullptr;
AdcRingBuffer*  ringBuffer = nullptr;
AzureIoTClient* azureClient = nullptr;
WebUIServer*    webServer = nullptr;

// Task handles
TaskHandle_t    adcTaskHandle = nullptr;
TaskHandle_t    telemetryTaskHandle = nullptr;
TaskHandle_t    webuiTaskHandle = nullptr;

// System state
volatile bool   systemRunning = true;
volatile bool   adcAvailable = false;
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
    LOG_PRINTF("ADC: %u | ", adc ? adc->getSampleCount() : 0);
    LOG_PRINTF("Frames: %u | ", ringBuffer ? ringBuffer->getFrameCount() : 0);
    LOG_PRINTF("Azure: %u | ", azureClient ? azureClient->getPublishCount() : 0);
    LOG_PRINTF("WS: %d | ", webServer ? webServer->getClientCount() : 0);
    LOG_PRINTF("GNSS: %s\n", gnssManager.hasValidLocation() ? "OK" : "N/A");
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

#if GNSS_ENABLE_AT_BOOT
    // Get GNSS fix BEFORE connecting to LTE
    // The modem cannot do both simultaneously, so we get location first
    LOG_PRINTLN("[Modem] Acquiring initial GNSS fix (before LTE)...");

    gnssManager.begin();

    if (gnssManager.acquireFix()) {
        const GnssLocation& loc = gnssManager.getLocation();
        LOG_PRINTF("[Modem] GNSS fix acquired: %.6f, %.6f\n", loc.latitude, loc.longitude);
    } else {
        LOG_PRINTLN("[Modem] WARNING: Could not acquire GNSS fix - continuing without location");
    }
#endif

    // Initialize Azure IoT client
    if (azureClient && azureClient->begin()) {
        // Start telemetry task - pass ADC availability status
        azureClient->startTelemetryTask(ringBuffer, &telemetryTaskHandle, adcAvailable);
        LOG_PRINTF("[Modem] Telemetry started (ADC %s)\n", adcAvailable ? "online" : "offline");
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

    LOG_PRINTLN("\n\n[Boot] Starting Geode AGM...");
    printSystemInfo();

    // Allocate objects now that Arduino/FreeRTOS is initialized
    LOG_PRINTLN("[Setup] Allocating objects...");

    // Ring buffer - use heap (constructor is now safe)
    ringBuffer = new AdcRingBuffer();
    if (!ringBuffer) {
        LOG_PRINTLN("[Setup] FATAL: Failed to allocate ring buffer");
        while(1) { delay(1000); }
    }
    ringBuffer->begin();  // Initialize FreeRTOS semaphores
    LOG_PRINTLN("[Setup] Ring buffer OK");

    // SPI bus
    adcSPI = new SPIClass(FSPI);
    if (!adcSPI) {
        LOG_PRINTLN("[Setup] FATAL: Failed to allocate SPI");
        while(1) { delay(1000); }
    }

    // Initialize SPI for ADS1256
    LOG_PRINTLN("[Setup] Initializing SPI...");
    adcSPI->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_ADS_CS);

    // ADS1256
    adc = new ADS1256(*adcSPI);
    LOG_PRINTLN("[Setup] Initializing ADS1256...");
    adcAvailable = adc->begin();
    if (!adcAvailable) {
        LOG_PRINTLN("[Setup] WARNING: ADS1256 not available - will report to Azure");
    } else {
        LOG_PRINTLN("[Setup] ADS1256 ready");
    }

    // Web server
    webServer = new WebUIServer();
    LOG_PRINTLN("[Setup] Initializing Web UI server...");
    if (webServer->begin()) {
        LOG_PRINTF("[Setup] Web UI: http://%s\n", webServer->getIPAddress().toString().c_str());
    } else {
        LOG_PRINTLN("[Setup] ERROR: Web UI server failed to start");
    }

    // Azure client (WalterModem is static, no instance needed)
    azureClient = new AzureIoTClient();
    LOG_PRINTLN("[Setup] Azure client allocated");

    // IMPORTANT: Start modem task FIRST - LTE connection should be independent
    // of all other modules. Even if ADC/WebUI hang, we want cloud connectivity.
    LOG_PRINTLN("[Setup] Starting modem initialization task (LTE/Azure)...");
    xTaskCreatePinnedToCore(
        modemInitTask,
        "Modem_Init",
        TASK_STACK_MODEM,
        nullptr,
        TASK_PRIORITY_MODEM,
        nullptr,
        TASK_CORE_MODEM
    );

    // Small delay to let modem task start
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start WebSocket streaming task
    LOG_PRINTLN("[Setup] Starting WebUI streaming task...");
    webServer->startStreamingTask(ringBuffer, &webuiTaskHandle);

    // Small delay between task creations
    vTaskDelay(pdMS_TO_TICKS(50));

    // Start ADC sampling task (runs on Core 1, independent of Core 0 tasks)
    LOG_PRINTLN("[Setup] Starting ADC sampling task...");
    adc->startSamplingTask(ringBuffer, &adcTaskHandle);

    // Give tasks time to initialize
    vTaskDelay(pdMS_TO_TICKS(100));

    LOG_PRINTLN("[Setup] Initialization complete!");
    LOG_PRINTLN("[Setup] Connect to WiFi: " WIFI_AP_SSID);
    LOG_PRINTF("[Setup] Open http://%s in browser\n", webServer->getIPAddress().toString().c_str());
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void loop() {
    // Print periodic status
    printStatus();

    // Monitor heap and PSRAM usage
    static uint32_t lastHeapCheck = 0;
    if (millis() - lastHeapCheck > 30000) {
        lastHeapCheck = millis();

        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 20000) {
            LOG_PRINTF("[Warning] Low heap: %d bytes\n", freeHeap);
        }
    }

    // Handle serial commands for debugging
    if (DEBUG_SERIAL.available()) {
        char cmd = DEBUG_SERIAL.read();
        switch (cmd) {
            case 's':
            case 'S':
                LOG_PRINTLN("\n=== Status ===");
                LOG_PRINTF("ADC Samples:  %u\n", adc ? adc->getSampleCount() : 0);
                LOG_PRINTF("ADC Dropped:  %u\n", adc ? adc->getDroppedCount() : 0);
                LOG_PRINTF("Frames:       %u\n", ringBuffer ? ringBuffer->getFrameCount() : 0);
                LOG_PRINTF("Frames Drop:  %u\n", ringBuffer ? ringBuffer->getDroppedFrames() : 0);
                LOG_PRINTF("Azure Conn:   %s\n", (azureClient && azureClient->isConnected()) ? "Yes" : "No");
                LOG_PRINTF("Azure Pub:    %u\n", azureClient ? azureClient->getPublishCount() : 0);
                LOG_PRINTF("Azure Err:    %u\n", azureClient ? azureClient->getErrorCount() : 0);
                LOG_PRINTF("WS Clients:   %d\n", webServer ? webServer->getClientCount() : 0);
                LOG_PRINTF("Free Heap:    %d\n", ESP.getFreeHeap());
                LOG_PRINTF("Free PSRAM:   %d\n", ESP.getFreePsram());
                if (gnssManager.hasValidLocation()) {
                    const GnssLocation& loc = gnssManager.getLocation();
                    LOG_PRINTF("GNSS:         %.6f, %.6f (age: %lu sec)\n",
                              loc.latitude, loc.longitude,
                              gnssManager.getLocationAgeMs() / 1000);
                } else {
                    LOG_PRINTLN("GNSS:         No fix");
                }
                break;

            case 'r':
            case 'R':
                LOG_PRINTLN("\n=== Restarting... ===");
                ESP.restart();
                break;

            case 'd':
            case 'D':
                LOG_PRINTLN("\n=== Running ADS1256 Diagnostic ===");
                LOG_PRINTLN("This will test multiple SPI speeds.");
                LOG_PRINTLN("WARNING: ADC sampling will be disrupted!\n");
                // Stop ADC task before diagnostic
                if (adc) {
                    adc->stopSamplingTask();
                }
                runAdsDiagnostic();
                LOG_PRINTLN("\n=== Diagnostic Complete ===");
                LOG_PRINTLN("Press 'r' to restart with normal operation.");
                break;

            case 'h':
            case 'H':
            case '?':
                LOG_PRINTLN("\n=== Commands ===");
                LOG_PRINTLN("  s - Status");
                LOG_PRINTLN("  d - ADC Diagnostic (tests SPI speeds)");
                LOG_PRINTLN("  r - Restart");
                LOG_PRINTLN("  h - Help");
                break;
        }
    }

    // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(100));
}
