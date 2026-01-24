# Walter Feels Hardware Tester

Comprehensive hardware test program for the **Walter Feels** expansion board (ESP32-S3 + Sequans Walter module).

## Features Tested

| Component | I2C Address | Description |
|-----------|-------------|-------------|
| SSD1306 OLED | 0x3C | 128x64 display for visual feedback |
| HDC1080 | 0x40 | Temperature and humidity sensor |
| LPS22HB | 0x5C | Barometric pressure sensor |
| SCD30 | 0x61 | CO2, temperature, humidity (secondary I2C) |
| LTC4015 | 0x68 | Battery charger, solar MPPT, power management |

### GPIO and Interfaces

- **GPIO A** (pin 39): Toggles at 1 Hz
- **GPIO B** (pin 38): Toggles at 5 Hz
- **Power Rails**: 3.3V (soft-start), 12V, I2C bus power
- **Serial Modes**: RS232, RS485, SDI-12
- **CAN Bus**: Transceiver enable/disable
- **SD Card**: Pin verification

## Hardware Requirements

- Walter module (ESP32-S3 + Sequans GM02SP)
- Walter Feels carrier board
- SSD1306 128x64 OLED display (connected to I2C port)
- USB-C cable for programming
- (Optional) LTE antenna if testing modem
- (Optional) Battery for power tests

## Pin Configuration

```
Primary I2C:    SDA=42, SCL=2
CO2 I2C:        SDA=12, SCL=11, EN=13
Power Control:  3V3=0, 12V=43, I2C_PWR=1
GPIO:           A=39, B=38
RS232:          TX_EN=17, RX_EN=16
RS485:          TX_EN=18, RX_EN=8
SDI-12:         TX_EN=10, RX_EN=9
CAN:            EN=44, RX=7, TX=15
SD Card:        CMD=6, CLK=5, DAT0=4
UART:           RX=41, TX=40
```

## Building and Flashing

### Prerequisites

```bash
# Install PlatformIO CLI
pip install platformio

# Or via Homebrew on macOS
brew install platformio
```

### Build

```bash
cd walter_feels_tester
pio run
```

### Flash via USB-C

```bash
# Put ESP32-S3 in download mode (hold BOOT, press RESET)
pio run --target upload

# Or with specific port
pio run --target upload --upload-port /dev/cu.usbmodem*
```

### Monitor Serial Output

```bash
pio device monitor --baud 921600

# Or combined build + upload + monitor
pio run --target upload --target monitor
```

## Usage

### Test Results

On power-up, the tester automatically:
1. Scans I2C buses for devices
2. Initializes OLED display
3. Runs all hardware tests
4. Displays pass/fail summary on OLED
5. Prints detailed results to Serial (921600 baud)
6. Enters continuous monitoring mode

### Serial Commands

| Key | Command |
|-----|---------|
| `r` | Re-run all hardware tests |
| `s` | Read all sensor values |
| `p` | Show power/battery status |
| `i` | Scan I2C buses |
| `g` | GPIO status |
| `h` | Help |

### Test Output Example

```
╔════════════════════════════════════════════════════════════╗
║            WALTER FEELS HARDWARE TESTER                    ║
║                    Version 1.0.0                           ║
╠════════════════════════════════════════════════════════════╣
║  Platform: ESP32-S3 + Sequans Walter                       ║
║  Board: Walter Feels Expansion                             ║
╚════════════════════════════════════════════════════════════╝

[01/12] I2C Bus Scan: PASS - Primary: 4, CO2 bus: 1 devices
[02/12] OLED Display: PASS - 128x64 @ 0x3C
[03/12] HDC1080: PASS - 23.5C, 45.2% RH
[04/12] LPS22HB: PASS - 1013.2 hPa, 23.8C
[05/12] SCD30: PASS - FW: 0.67, CO2: 412 ppm
[06/12] LTC4015: PASS - Vbat:3.85 Vin:5.12 Vsys:3.85V
...

--- TEST SUMMARY ---
Total:  12
Passed: 12
Failed: 0
Skipped: 0

Overall: PASS
--------------------
```

## IDE Recommendations

For a complete development workflow with better debugging, simulation, and project management:

### VSCode + PlatformIO (Recommended)
- **Install**: VSCode + PlatformIO IDE extension
- **Features**: IntelliSense, integrated terminal, one-click build/upload, serial monitor
- **Debug**: Supports ESP32 JTAG debugging (with adapter)

```bash
# Open project in VSCode
code walter_feels_tester/
```

### CLion + PlatformIO
- Professional IDE with better refactoring support
- Excellent CMake integration
- Superior debugger interface

### ESP-IDF with VSCode
- Full ESP-IDF SDK access
- Advanced debugging and analysis tools
- More complex but more powerful

## Extending the Tester

### Adding New Tests

1. Add test function to `test_suite.h`:
```cpp
static TestResult_t testMyFeature(char* msg, size_t len);
```

2. Implement in `test_suite.cpp`:
```cpp
TestResult_t TestSuite::testMyFeature(char* msg, size_t len) {
    // Your test logic
    snprintf(msg, len, "Result details");
    return TEST_PASS;
}
```

3. Register in `main.cpp`:
```cpp
testSuite->registerTest("My Feature", "Description", TestSuite::testMyFeature);
```

## Troubleshooting

### No Serial Output
- Check USB-C cable (data cable, not charge-only)
- Verify correct port: `ls /dev/cu.usb*` (macOS) or `ls /dev/ttyUSB*` (Linux)
- Reset the board after flashing

### OLED Not Detected
- Verify I2C address (0x3C or 0x78 depending on notation)
- Check I2C connections (SDA to pin 42, SCL to pin 2)
- Ensure I2C bus power is enabled

### Sensors Not Found
- Power cycle the board
- Check that I2C bus power is enabled (pin 1)
- Verify sensor voltages (3.3V)

## License

GPLv3 - Matching the Walter hardware open source license.

## Resources

- [Walter Hardware GitHub](https://github.com/QuickSpot/walter-hardware)
- [Walter Arduino Library](https://github.com/QuickSpot/walter-arduino)
- [QuickSpot Documentation](https://docs.quickspot.io)
