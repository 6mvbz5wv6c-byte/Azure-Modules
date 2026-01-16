/**
 * @file webui.h
 * @brief Web UI server for local visualization via WiFi AP
 *
 * Creates a WiFi Access Point "Geode_AGM" and serves a real-time
 * oscilloscope web interface with WebSocket streaming of ADC data.
 */

#ifndef WEBUI_H
#define WEBUI_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "ring_buffer.h"

// =============================================================================
// WEB UI SERVER
// =============================================================================

class WebUIServer {
public:
    /**
     * @brief Constructor
     */
    WebUIServer();

    /**
     * @brief Destructor
     */
    ~WebUIServer();

    /**
     * @brief Initialize WiFi AP and web server
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Stop the web server
     */
    void stop();

    /**
     * @brief Start WebSocket streaming task
     * @param ringBuffer Ring buffer to stream from
     * @param taskHandle Output task handle
     */
    void startStreamingTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle);

    /**
     * @brief Stop streaming task
     */
    void stopStreamingTask();

    /**
     * @brief Get number of connected WebSocket clients
     */
    uint8_t getClientCount() const;

    /**
     * @brief Get IP address of AP
     */
    IPAddress getIPAddress() const;

    /**
     * @brief Broadcast frame to all WebSocket clients
     * @param frame ADC frame to broadcast
     */
    void broadcastFrame(const AdcFrame& frame);

private:
    // HTTP request handlers
    static void handleRoot(AsyncWebServerRequest* request);
    static void handleNotFound(AsyncWebServerRequest* request);

    // WebSocket event handler
    static void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                  AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Static instance for callbacks
    static WebUIServer* _instance;

    // Server components
    AsyncWebServer* _server;
    AsyncWebSocket* _ws;

    // State
    bool            _running;

    // Streaming task
    TaskHandle_t    _streamTask;
    AdcRingBuffer*  _ringBuffer;
    volatile bool   _stopRequested;

    // Task function
    static void streamingTaskFunc(void* param);

    // JSON buffer for WebSocket messages
    char*           _jsonBuffer;
    static constexpr size_t JSON_BUFFER_SIZE = 8192;
};

// =============================================================================
// EMBEDDED WEB PAGE (PROGMEM)
// =============================================================================

extern const char INDEX_HTML[] PROGMEM;
extern const size_t INDEX_HTML_LEN;

#endif // WEBUI_H
