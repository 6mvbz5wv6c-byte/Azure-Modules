/**
 * @file config.h
 * @brief Configuration and pin definitions for Geode AGM Walter ESP32-S3
 *
 * Hardware: DPTechnics Walter (ESP32-S3-WROOM-1-N16R2 + Sequans GM02SP)
 * ADC: ADS1256 24-bit Delta-Sigma ADC
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// SAMPLING CONFIGURATION
// =============================================================================

#ifndef SAMPLE_RATE_HZ
#define SAMPLE_RATE_HZ          1000        // Samples per second
#endif

#ifndef FRAME_SIZE
#define FRAME_SIZE              1000        // Samples per frame (1 second)
#endif

#define SAMPLE_PERIOD_US        (1000000 / SAMPLE_RATE_HZ)  // 1000us for 1kHz

// Ring buffer size (frames) - must be power of 2
#define RING_BUFFER_FRAMES      8           // ~8 seconds of buffering

// =============================================================================
// SPI PIN DEFINITIONS (ADS1256)
// =============================================================================
// Using VSPI (SPI3) on ESP32-S3

#define PIN_SPI_MOSI            11          // GPIO11 - SPI MOSI
#define PIN_SPI_MISO            13          // GPIO13 - SPI MISO
#define PIN_SPI_SCK             12          // GPIO12 - SPI Clock
#define PIN_ADS_CS              10          // GPIO10 - ADS1256 Chip Select
#define PIN_ADS_DRDY            9           // GPIO9  - ADS1256 Data Ready (interrupt)
#define PIN_ADS_RST             14          // GPIO14 - ADS1256 Reset
#define PIN_ADS_PDWN            -1          // Not connected (tied high)

// SPI Configuration
#define ADS_SPI_FREQ            2000000     // 2 MHz SPI clock (ADS1256 max ~2.5MHz)
#define ADS_SPI_MODE            SPI_MODE1   // CPOL=0, CPHA=1

// =============================================================================
// WIFI ACCESS POINT CONFIGURATION
// =============================================================================

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID            "Geode_AGM"
#endif

#define WIFI_AP_PASS            ""          // No password
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_MAX_CONN        4
#define WEBSERVER_PORT          80
#define WEBSOCKET_PATH          "/ws"

// =============================================================================
// AZURE IOT HUB CONFIGURATION
// =============================================================================

#ifndef AZURE_IOT_HUB_HOST
#define AZURE_IOT_HUB_HOST      "PaidSense.azure-devices.net"
#endif

#ifndef AZURE_DEVICE_ID
#define AZURE_DEVICE_ID         "Geode_Walt_0001"
#endif

// Shared Access Key (base64 encoded)
#define AZURE_SAS_KEY           "nVXT3Qi9FA2ryVLSlq7Hvox82HLNICYjytZfXWPmTbw="

// MQTT Configuration for Azure IoT Hub
#define AZURE_MQTT_PORT         8883        // TLS
#define AZURE_MQTT_KEEPALIVE    60          // seconds
#define AZURE_SAS_TTL_HOURS     24          // SAS token validity

// MQTT Topics
// devices/{device-id}/messages/events/
#define AZURE_TELEMETRY_TOPIC   "devices/" AZURE_DEVICE_ID "/messages/events/"

// =============================================================================
// LTE MODEM CONFIGURATION (Sequans GM02SP via Walter)
// =============================================================================

// APN Configuration - Adjust for your carrier
#define LTE_APN                 "osc"  // Example: 1NCE IoT
#define LTE_APN_USER            ""
#define LTE_APN_PASS            ""

// Modem UART (internal Walter connection)
// Walter uses Serial2 internally for modem communication

// =============================================================================
// GNSS CONFIGURATION
// =============================================================================

// GNSS fix timeout (seconds) - max time to wait for a fix
// Cold start can take 30-60+ seconds, up to 12+ minutes in poor conditions
#define GNSS_FIX_TIMEOUT_SEC    180  // 3 minutes total

// GNSS fix attempts - number of retries before giving up
// Each attempt will get at least 30 seconds
#define GNSS_FIX_MAX_ATTEMPTS   3

// GNSS confidence threshold (meters) - reject fixes with higher uncertainty
#define GNSS_MAX_CONFIDENCE     100.0f

// GNSS update interval (milliseconds) - how often to refresh location
// 30 minutes = 1800000ms. Set to 0 to disable periodic updates.
#define GNSS_UPDATE_INTERVAL_MS 1800000

// Enable GNSS at boot (recommended for first-fix)
#define GNSS_ENABLE_AT_BOOT     1

// =============================================================================
// FREERTOS TASK CONFIGURATION
// =============================================================================

// Task priorities (higher = more priority, max = configMAX_PRIORITIES-1)
#define TASK_PRIORITY_ADC       5           // Highest - critical timing
#define TASK_PRIORITY_TELEMETRY 3           // High - cloud upload
#define TASK_PRIORITY_WEBUI     2           // Medium - local display
#define TASK_PRIORITY_MODEM     2           // Medium - connection management

// Task stack sizes (in words, not bytes)
#define TASK_STACK_ADC          4096
#define TASK_STACK_TELEMETRY    8192        // Needs space for JSON/base64
#define TASK_STACK_WEBUI        8192        // Web server needs stack
#define TASK_STACK_MODEM        4096

// Core assignments (ESP32-S3 has 2 cores: 0 and 1)
#define TASK_CORE_ADC           1           // Core 1 for ADC (no WiFi interrupt)
#define TASK_CORE_TELEMETRY     0           // Core 0
#define TASK_CORE_WEBUI         0           // Core 0 with WiFi stack
#define TASK_CORE_MODEM         0           // Core 0

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================

#define DEBUG_SERIAL            Serial      // USB CDC
#define DEBUG_BAUD              921600

// Debug macros
#ifdef DEBUG_ENABLED
    #define DBG_PRINT(...)      DEBUG_SERIAL.print(__VA_ARGS__)
    #define DBG_PRINTLN(...)    DEBUG_SERIAL.println(__VA_ARGS__)
    #define DBG_PRINTF(...)     DEBUG_SERIAL.printf(__VA_ARGS__)
#else
    #define DBG_PRINT(...)
    #define DBG_PRINTLN(...)
    #define DBG_PRINTF(...)
#endif

// Always-on logging
#define LOG_PRINT(...)          DEBUG_SERIAL.print(__VA_ARGS__)
#define LOG_PRINTLN(...)        DEBUG_SERIAL.println(__VA_ARGS__)
#define LOG_PRINTF(...)         DEBUG_SERIAL.printf(__VA_ARGS__)

// =============================================================================
// ADS1256 REGISTER DEFINITIONS
// =============================================================================

// Register addresses
#define ADS_REG_STATUS          0x00
#define ADS_REG_MUX             0x01
#define ADS_REG_ADCON           0x02
#define ADS_REG_DRATE           0x03
#define ADS_REG_IO              0x04
#define ADS_REG_OFC0            0x05
#define ADS_REG_OFC1            0x06
#define ADS_REG_OFC2            0x07
#define ADS_REG_FSC0            0x08
#define ADS_REG_FSC1            0x09
#define ADS_REG_FSC2            0x0A

// Commands
#define ADS_CMD_WAKEUP          0x00
#define ADS_CMD_RDATA           0x01
#define ADS_CMD_RDATAC          0x03
#define ADS_CMD_SDATAC          0x0F
#define ADS_CMD_RREG            0x10
#define ADS_CMD_WREG            0x50
#define ADS_CMD_SELFCAL         0xF0
#define ADS_CMD_SELFOCAL        0xF1
#define ADS_CMD_SELFGCAL        0xF2
#define ADS_CMD_SYSOCAL         0xF3
#define ADS_CMD_SYSGCAL         0xF4
#define ADS_CMD_SYNC            0xFC
#define ADS_CMD_STANDBY         0xFD
#define ADS_CMD_RESET           0xFE

// Data rates
#define ADS_DRATE_30000SPS      0xF0
#define ADS_DRATE_15000SPS      0xE0
#define ADS_DRATE_7500SPS       0xD0
#define ADS_DRATE_3750SPS       0xC0
#define ADS_DRATE_2000SPS       0xB0
#define ADS_DRATE_1000SPS       0xA1
#define ADS_DRATE_500SPS        0x92
#define ADS_DRATE_100SPS        0x82
#define ADS_DRATE_60SPS         0x72
#define ADS_DRATE_50SPS         0x63
#define ADS_DRATE_30SPS         0x53
#define ADS_DRATE_25SPS         0x43
#define ADS_DRATE_15SPS         0x33
#define ADS_DRATE_10SPS         0x23
#define ADS_DRATE_5SPS          0x13
#define ADS_DRATE_2_5SPS        0x03

// Gain values
#define ADS_GAIN_1              0
#define ADS_GAIN_2              1
#define ADS_GAIN_4              2
#define ADS_GAIN_8              3
#define ADS_GAIN_16             4
#define ADS_GAIN_32             5
#define ADS_GAIN_64             6

// MUX channel definitions (differential)
#define ADS_MUX_DIFF_0_1        ((0 << 4) | 1)  // AIN0-AIN1
#define ADS_MUX_DIFF_2_3        ((2 << 4) | 3)  // AIN2-AIN3
#define ADS_MUX_DIFF_4_5        ((4 << 4) | 5)  // AIN4-AIN5
#define ADS_MUX_DIFF_6_7        ((6 << 4) | 7)  // AIN6-AIN7

#endif // CONFIG_H
