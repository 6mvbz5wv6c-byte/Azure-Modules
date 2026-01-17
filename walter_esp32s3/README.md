# Geode AGM - Walter ESP32-S3 ADC Sampler

High-speed ADC data acquisition system for the **DPTechnics Walter** module (ESP32-S3 + Sequans GM02SP LTE modem).

## Features

- **ADS1256 24-bit ADC** sampling at 1000 SPS with hardware interrupt-driven DRDY handling
- **Zero-drop ring buffer** - FreeRTOS-optimized circular buffer ensures no samples are missed
- **Local Web UI** - WiFi AP mode ("Geode_AGM") with real-time oscilloscope visualization
- **Azure IoT Hub Telemetry** - Sends ADC frames over LTE with SAS token authentication
- **Advanced DSP Filters** - Butterworth bandpass, 60Hz notch, and Lock-In amplifier
- **FreeRTOS Architecture** - Proper task separation for deterministic sampling

## Hardware Requirements

- **Walter Module** (ESP32-S3-WROOM-1-N16R2 + Sequans GM02SP)
  - [DPTechnics Walter](https://www.quickspot.io/)
  - 16MB Flash, 2MB PSRAM
  - Integrated LTE-M/NB-IoT modem with GNSS

- **ADS1256 ADC Board**
  - 24-bit Delta-Sigma ADC
  - Up to 30,000 SPS (configured for 1000 SPS)
  - Programmable gain amplifier (1x to 64x)

## Pin Connections

| Signal | Walter GPIO | Description |
|--------|-------------|-------------|
| SPI MOSI | GPIO11 | ADS1256 DIN |
| SPI MISO | GPIO13 | ADS1256 DOUT |
| SPI SCK | GPIO12 | ADS1256 SCLK |
| CS | GPIO10 | ADS1256 Chip Select |
| DRDY | GPIO9 | ADS1256 Data Ready (interrupt) |
| RST | GPIO14 | ADS1256 Reset |

## Software Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         FreeRTOS Tasks                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │   ADC Task      │  │  Telemetry Task │  │   WebUI Task    │ │
│  │   (Core 1)      │  │    (Core 0)     │  │    (Core 0)     │ │
│  │   Priority 5    │  │    Priority 3   │  │    Priority 2   │ │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘ │
│           │                    │                    │          │
│           ▼                    ▼                    ▼          │
│  ┌─────────────────────────────────────────────────────────────┤
│  │              Thread-Safe Ring Buffer (8 frames)             │
│  └─────────────────────────────────────────────────────────────┤
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                         Hardware Layer                          │
├─────────────────────────────────────────────────────────────────┤
│  ADS1256 (SPI)  │  WiFi AP  │  Sequans GM02SP (LTE/MQTT)      │
└─────────────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

1. Install [PlatformIO](https://platformio.org/)
2. Clone this repository
3. Install the Walter Arduino library:
   ```bash
   cd ~/.platformio/lib
   git clone https://github.com/QuickSpot/walter-arduino.git
   ```

### Build and Upload

```bash
cd walter_esp32s3
pio run -t upload
```

### Monitor Serial Output

```bash
pio device monitor -b 921600
```

## Configuration

Edit `include/config.h` to customize:

- **Sampling**: `SAMPLE_RATE_HZ`, `FRAME_SIZE`
- **WiFi AP**: `WIFI_AP_SSID`, `WIFI_AP_PASS`
- **Azure IoT**: `AZURE_IOT_HUB_HOST`, `AZURE_DEVICE_ID`, `AZURE_SAS_KEY`
- **LTE**: `LTE_APN`, `LTE_APN_USER`, `LTE_APN_PASS`
- **GPIO Pins**: SPI and control pin assignments

## Usage

### Local Web Interface

1. Connect to WiFi: **Geode_AGM** (no password)
2. Open browser: `http://192.168.4.1`
3. View real-time oscilloscope with DSP filters

### Azure IoT Hub

Data is automatically streamed to Azure IoT Hub over LTE. Each frame contains:

```json
{
  "deviceId": "Geode_Walt_0001",
  "frameIndex": 12345,
  "startUs": 1704067200000000,
  "endUs": 1704067200999000,
  "sampleRate": 1000.0,
  "numSamples": 1000,
  "samplesB64": "base64-encoded-int32-LE-samples"
}
```

### Serial Debug Commands

- `s` - Print status
- `r` - Restart device
- `h` - Show help

## DSP Filters (Web UI)

The web interface includes professional-grade digital signal processing:

- **Butterworth Bandpass** - N-th order flat-top response
- **60Hz Notch Filter** - Cascaded notches with harmonic rejection
- **Lock-In Amplifier** - Phase-sensitive detection for extracting weak signals

## Project Structure

```
walter_esp32s3/
├── platformio.ini          # Build configuration
├── include/
│   ├── config.h            # System configuration
│   ├── ring_buffer.h       # Thread-safe ring buffer
│   ├── ads1256.h           # ADC driver header
│   ├── azure_iot.h         # Azure IoT client header
│   └── webui.h             # Web server header
├── src/
│   ├── main.cpp            # Application entry point
│   ├── ads1256.cpp         # ADC driver implementation
│   ├── azure_iot.cpp       # Azure IoT client
│   ├── webui.cpp           # Web server implementation
│   └── webui_html.cpp      # Embedded web UI HTML/JS
└── README.md
```

## License

MIT License - See LICENSE file for details.

## References

- [Walter Datasheet](https://www.quickspot.io/datasheet/walter_datasheet.pdf)
- [Walter Arduino Library](https://github.com/QuickSpot/walter-arduino)
- [ADS1256 Datasheet](https://www.ti.com/product/ADS1256)
- [Azure IoT Hub MQTT Protocol](https://learn.microsoft.com/en-us/azure/iot/iot-mqtt-connect-to-iot-hub)
- [ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
