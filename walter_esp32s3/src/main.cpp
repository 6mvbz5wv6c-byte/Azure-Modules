/**
 * @file main.cpp
 * @brief Geode AGM - Walter ESP32-S3 Main Application
 *
 * MINIMAL TEST VERSION - to debug boot loop
 */

#include <Arduino.h>

// Minimal test - just serial and WiFi AP
#include <WiFi.h>

#define DEBUG_SERIAL Serial
#define DEBUG_BAUD 921600

void setup() {
    // Initialize serial for debug output
    DEBUG_SERIAL.begin(DEBUG_BAUD);

    // Wait for USB CDC to initialize
    delay(3000);

    DEBUG_SERIAL.println("\n\n");
    DEBUG_SERIAL.println("========================================");
    DEBUG_SERIAL.println("  MINIMAL TEST - Geode AGM");
    DEBUG_SERIAL.println("========================================");
    DEBUG_SERIAL.printf("  CPU Freq:    %d MHz\n", getCpuFrequencyMhz());
    DEBUG_SERIAL.printf("  Free Heap:   %d bytes\n", ESP.getFreeHeap());
    DEBUG_SERIAL.printf("  Free PSRAM:  %d bytes\n", ESP.getFreePsram());
    DEBUG_SERIAL.printf("  Chip Model:  %s\n", ESP.getChipModel());
    DEBUG_SERIAL.println("========================================\n");

    // Try starting WiFi AP
    DEBUG_SERIAL.println("[Setup] Starting WiFi AP...");
    WiFi.mode(WIFI_AP);

    bool apStarted = WiFi.softAP("Geode_Test", "", 6, 0, 4);

    if (apStarted) {
        DEBUG_SERIAL.printf("[Setup] WiFi AP started! SSID: Geode_Test\n");
        DEBUG_SERIAL.printf("[Setup] IP Address: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        DEBUG_SERIAL.println("[Setup] ERROR: Failed to start WiFi AP");
    }

    DEBUG_SERIAL.println("\n[Setup] Initialization complete!");
    DEBUG_SERIAL.println("[Setup] If you see this, basic boot is working.\n");
}

void loop() {
    static uint32_t lastPrint = 0;
    uint32_t now = millis();

    if (now - lastPrint >= 5000) {
        lastPrint = now;
        DEBUG_SERIAL.printf("[Loop] Uptime: %d seconds, Heap: %d, Clients: %d\n",
            now / 1000, ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    }

    delay(100);
}
