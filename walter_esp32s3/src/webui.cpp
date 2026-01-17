/**
 * @file webui.cpp
 * @brief Web UI server implementation
 *
 * Implements WiFi AP mode and WebSocket streaming for real-time
 * oscilloscope visualization in a web browser.
 */

#include "webui.h"
#include <ArduinoJson.h>

// Static instance
WebUIServer* WebUIServer::_instance = nullptr;

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

WebUIServer::WebUIServer()
    : _server(nullptr)
    , _ws(nullptr)
    , _running(false)
    , _streamTask(nullptr)
    , _ringBuffer(nullptr)
    , _stopRequested(false)
    , _jsonBuffer(nullptr)
{
    _instance = this;
}

WebUIServer::~WebUIServer() {
    stop();
    if (_jsonBuffer) {
        free(_jsonBuffer);
        _jsonBuffer = nullptr;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool WebUIServer::begin() {
    LOG_PRINTLN("[WebUI] Starting WiFi Access Point...");

    // Allocate JSON buffer
    _jsonBuffer = (char*)ps_malloc(JSON_BUFFER_SIZE);
    if (!_jsonBuffer) {
        _jsonBuffer = (char*)malloc(JSON_BUFFER_SIZE);
    }

    if (!_jsonBuffer) {
        LOG_PRINTLN("[WebUI] ERROR: Failed to allocate JSON buffer");
        return false;
    }

    // Configure WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);

    IPAddress apIP = WiFi.softAPIP();
    LOG_PRINTF("[WebUI] AP started: SSID=%s, IP=%s\n", WIFI_AP_SSID, apIP.toString().c_str());

    // Create web server
    _server = new AsyncWebServer(WEBSERVER_PORT);

    // Create WebSocket
    _ws = new AsyncWebSocket(WEBSOCKET_PATH);
    _ws->onEvent(onWebSocketEvent);
    _server->addHandler(_ws);

    // Serve index.html
    _server->on("/", HTTP_GET, handleRoot);

    // 404 handler
    _server->onNotFound(handleNotFound);

    // Start server
    _server->begin();
    _running = true;

    LOG_PRINTF("[WebUI] Web server started on port %d\n", WEBSERVER_PORT);
    LOG_PRINTF("[WebUI] WebSocket available at ws://%s%s\n", apIP.toString().c_str(), WEBSOCKET_PATH);

    return true;
}

void WebUIServer::stop() {
    stopStreamingTask();

    if (_ws) {
        _ws->closeAll();
        delete _ws;
        _ws = nullptr;
    }

    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }

    WiFi.softAPdisconnect(true);
    _running = false;

    LOG_PRINTLN("[WebUI] Server stopped");
}

// =============================================================================
// HTTP HANDLERS
// =============================================================================

void WebUIServer::handleRoot(AsyncWebServerRequest* request) {
    // Use send() with PROGMEM string - cast to uint8_t* for API compatibility
    request->send(200, "text/html", INDEX_HTML);
}

void WebUIServer::handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not Found");
}

// =============================================================================
// WEBSOCKET HANDLERS
// =============================================================================

void WebUIServer::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            LOG_PRINTF("[WebUI] WebSocket client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            LOG_PRINTF("[WebUI] WebSocket client #%u disconnected\n", client->id());
            break;

        case WS_EVT_ERROR:
            LOG_PRINTF("[WebUI] WebSocket client #%u error: %s\n", client->id(), (char*)data);
            break;

        case WS_EVT_PONG:
            break;

        case WS_EVT_DATA:
            // Handle incoming messages (e.g., PGA control commands)
            {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                    data[len] = 0;
                    LOG_PRINTF("[WebUI] Received: %s\n", (char*)data);
                    // TODO: Parse and handle control commands
                }
            }
            break;
    }
}

// =============================================================================
// STREAMING
// =============================================================================

uint8_t WebUIServer::getClientCount() const {
    return _ws ? _ws->count() : 0;
}

IPAddress WebUIServer::getIPAddress() const {
    return WiFi.softAPIP();
}

void WebUIServer::broadcastFrame(const AdcFrame& frame) {
    if (!_ws || _ws->count() == 0) {
        return;  // No clients connected
    }

    // Debug: Log first broadcast
    static bool firstBroadcast = true;
    if (firstBroadcast) {
        LOG_PRINTF("[WebUI] First frame broadcast: %u samples, first=%d, last=%d\n",
                  frame.numSamples,
                  frame.numSamples > 0 ? frame.samples[0] : 0,
                  frame.numSamples > 0 ? frame.samples[frame.numSamples - 1] : 0);
        firstBroadcast = false;
    }

    // Build JSON message
    // Format: {"t0": startTime, "geo": [sample1, sample2, ...]}

    // Build samples array as comma-separated values
    char* p = _jsonBuffer;
    p += snprintf(p, JSON_BUFFER_SIZE, "{\"t0\":%.3f,\"geo\":[",
                  (double)frame.startTimeUs / 1000000.0);

    // Add samples
    for (uint16_t i = 0; i < frame.numSamples; i++) {
        if (i > 0) {
            *p++ = ',';
        }
        p += snprintf(p, JSON_BUFFER_SIZE - (p - _jsonBuffer), "%d", frame.samples[i]);

        // Safety check
        if ((p - _jsonBuffer) > JSON_BUFFER_SIZE - 32) {
            LOG_PRINTLN("[WebUI] WARNING: JSON buffer overflow");
            break;
        }
    }

    // Close JSON
    p += snprintf(p, JSON_BUFFER_SIZE - (p - _jsonBuffer), "]}");

    size_t jsonLen = p - _jsonBuffer;

    // Broadcast to all clients
    _ws->textAll(_jsonBuffer, jsonLen);
}

// =============================================================================
// STREAMING TASK
// =============================================================================

void WebUIServer::startStreamingTask(AdcRingBuffer* ringBuffer, TaskHandle_t* taskHandle) {
    _ringBuffer = ringBuffer;
    _stopRequested = false;

    xTaskCreatePinnedToCore(
        streamingTaskFunc,
        "WebUI_Stream",
        TASK_STACK_WEBUI,
        this,
        TASK_PRIORITY_WEBUI,
        &_streamTask,
        TASK_CORE_WEBUI
    );

    if (taskHandle) {
        *taskHandle = _streamTask;
    }

    LOG_PRINTLN("[WebUI] Streaming task started");
}

void WebUIServer::stopStreamingTask() {
    _stopRequested = true;

    if (_streamTask) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _streamTask = nullptr;
    }

    LOG_PRINTLN("[WebUI] Streaming task stopped");
}

void WebUIServer::streamingTaskFunc(void* param) {
    WebUIServer* server = static_cast<WebUIServer*>(param);

    LOG_PRINTLN("[WebUI Task] Starting streaming loop");

    // Create buffer cursor
    BufferCursor cursor(*server->_ringBuffer);
    cursor.reset();

    uint32_t framesSent = 0;
    uint32_t lastLogTime = millis();

    while (!server->_stopRequested) {
        // Clean up disconnected clients periodically
        if (server->_ws) {
            server->_ws->cleanupClients();
        }

        // Wait for and stream frames
        AdcFrame frame;
        if (cursor.waitAndRead(frame, 100)) {
            server->broadcastFrame(frame);
            framesSent++;

            // Log every 10 seconds
            if (millis() - lastLogTime > 10000) {
                LOG_PRINTF("[WebUI Task] Sent %u frames, %d clients, buffer has %u frames\n",
                          framesSent, server->_ws ? server->_ws->count() : 0,
                          server->_ringBuffer->getFrameCount());
                lastLogTime = millis();
            }
        }

        // Small delay to prevent overwhelming clients
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    LOG_PRINTLN("[WebUI Task] Streaming loop exited");
    vTaskDelete(nullptr);
}
