/**
 * @file config.h
 * @brief Walter Feels Hardware Tester - Pin Definitions and Configuration
 *
 * Based on Walter Feels schematic v2.6 and QuickSpot reference design.
 * This header defines all GPIO pins, I2C addresses, and test parameters.
 */

#ifndef WALTER_FEELS_CONFIG_H
#define WALTER_FEELS_CONFIG_H

#include <Arduino.h>

// =============================================================================
// VERSION INFO
// =============================================================================
#define TESTER_VERSION "1.0.0"
#define TESTER_NAME "Walter Feels Hardware Tester"

// =============================================================================
// POWER MANAGEMENT PINS
// =============================================================================
#define WFEELS_PIN_3V3_EN           0   // 3.3V rail enable (PWM soft-start)
#define WFEELS_PIN_12V_EN           43  // 12V rail enable
#define WFEELS_PIN_I2C_BUS_PWR      1   // I2C bus power enable

// =============================================================================
// PRIMARY I2C BUS (Wire) - Sensors and Display
// =============================================================================
#define WFEELS_PIN_I2C_SDA          42
#define WFEELS_PIN_I2C_SCL          2
#define WFEELS_I2C_FREQ             100000  // 100kHz

// =============================================================================
// SECONDARY I2C BUS (Wire1) - CO2 Sensor
// =============================================================================
#define WFEELS_PIN_CO2_SDA          12
#define WFEELS_PIN_CO2_SCL          11
#define WFEELS_PIN_CO2_EN           13  // CO2 sensor power enable

// =============================================================================
// SERIAL INTERFACE PINS
// =============================================================================
// RS232 Transceiver
#define WFEELS_PIN_RS232_TX_EN      17
#define WFEELS_PIN_RS232_RX_EN      16

// RS485 Transceiver
#define WFEELS_PIN_RS485_TX_EN      18
#define WFEELS_PIN_RS485_RX_EN      8

// SDI-12 Interface
#define WFEELS_PIN_SDI12_TX_EN      10
#define WFEELS_PIN_SDI12_RX_EN      9

// UART pins for serial interfaces
#define WFEELS_PIN_UART_RX          41
#define WFEELS_PIN_UART_TX          40

// =============================================================================
// CAN BUS
// =============================================================================
#define WFEELS_PIN_CAN_EN           44
#define WFEELS_PIN_CAN_RX           7
#define WFEELS_PIN_CAN_TX           15

// =============================================================================
// SD CARD INTERFACE
// =============================================================================
#define WFEELS_PIN_SD_CMD           6
#define WFEELS_PIN_SD_CLK           5
#define WFEELS_PIN_SD_DAT0          4

// =============================================================================
// GENERAL PURPOSE I/O
// =============================================================================
#define WFEELS_PIN_GPIO_A           39
#define WFEELS_PIN_GPIO_B           38

// GPIO Toggle frequencies (in Hz)
#define GPIO_A_TOGGLE_FREQ          1   // 1 Hz (toggle every 500ms)
#define GPIO_B_TOGGLE_FREQ          5   // 5 Hz (toggle every 100ms)

// =============================================================================
// LTC4015 BATTERY CHARGER (on primary I2C)
// =============================================================================
#define LTC4015_I2C_ADDR            0x68  // 7-bit address (0xD0 >> 1)
#define WFEELS_PIN_LTC4015_SMBALERT 3
#define WFEELS_PIN_LTC4015_MPPT     4

// =============================================================================
// SENSOR I2C ADDRESSES
// =============================================================================
#define HDC1080_I2C_ADDR            0x40  // Temperature/Humidity
#define LPS22HB_I2C_ADDR            0x5C  // Barometric Pressure
#define SCD30_I2C_ADDR              0x61  // CO2 (on secondary I2C)

// =============================================================================
// OLED DISPLAY (on primary I2C)
// =============================================================================
#define SSD1306_I2C_ADDR            0x3C  // User specified 0x78 = 0x3C in 7-bit
#define SSD1306_WIDTH               128
#define SSD1306_HEIGHT              64

// =============================================================================
// HDC1080 REGISTERS
// =============================================================================
#define HDC1080_REG_TEMPERATURE     0x00
#define HDC1080_REG_HUMIDITY        0x01
#define HDC1080_REG_CONFIG          0x02
#define HDC1080_REG_SERIAL_ID_1     0xFB
#define HDC1080_REG_SERIAL_ID_2     0xFC
#define HDC1080_REG_SERIAL_ID_3     0xFD
#define HDC1080_REG_MANUFACTURER    0xFE
#define HDC1080_REG_DEVICE_ID       0xFF

#define HDC1080_MANUFACTURER_ID     0x5449  // Texas Instruments
#define HDC1080_DEVICE_ID           0x1050

// =============================================================================
// LPS22HB REGISTERS
// =============================================================================
#define LPS22HB_REG_WHO_AM_I        0x0F
#define LPS22HB_REG_CTRL1           0x10
#define LPS22HB_REG_CTRL2           0x11
#define LPS22HB_REG_STATUS          0x27
#define LPS22HB_REG_PRESS_OUT_XL    0x28
#define LPS22HB_REG_PRESS_OUT_L     0x29
#define LPS22HB_REG_PRESS_OUT_H     0x2A
#define LPS22HB_REG_TEMP_OUT_L      0x2B
#define LPS22HB_REG_TEMP_OUT_H      0x2C

#define LPS22HB_WHO_AM_I_VALUE      0xB1

// =============================================================================
// SCD30 COMMANDS (I2C commands are 16-bit)
// =============================================================================
#define SCD30_CMD_START_MEASURE     0x0010
#define SCD30_CMD_STOP_MEASURE      0x0104
#define SCD30_CMD_SET_INTERVAL      0x4600
#define SCD30_CMD_DATA_READY        0x0202
#define SCD30_CMD_READ_MEASUREMENT  0x0300
#define SCD30_CMD_SET_AUTO_CAL      0x5306
#define SCD30_CMD_READ_FIRMWARE     0xD100
#define SCD30_CMD_SOFT_RESET        0xD304

// =============================================================================
// LTC4015 REGISTERS
// =============================================================================
#define LTC4015_REG_VBAT_LO_ALERT   0x01
#define LTC4015_REG_VBAT_HI_ALERT   0x02
#define LTC4015_REG_VIN_LO_ALERT    0x03
#define LTC4015_REG_VIN_HI_ALERT    0x04
#define LTC4015_REG_CONFIG_BITS     0x14
#define LTC4015_REG_CHARGER_STATE   0x34
#define LTC4015_REG_CHARGE_STATUS   0x35
#define LTC4015_REG_SYSTEM_STATUS   0x39
#define LTC4015_REG_VBAT            0x3A
#define LTC4015_REG_VIN             0x3B
#define LTC4015_REG_VSYS            0x3C
#define LTC4015_REG_IBAT            0x3D
#define LTC4015_REG_IIN             0x3E
#define LTC4015_REG_DIE_TEMP        0x3F
#define LTC4015_REG_CHEM_CELLS      0x43

// Chemistry types
#define LTC4015_CHEM_LIION          0x00
#define LTC4015_CHEM_LIFEPO4        0x01
#define LTC4015_CHEM_LEAD_ACID      0x02

// =============================================================================
// TEST PARAMETERS
// =============================================================================
#define TEST_TIMEOUT_MS             5000    // Timeout for each test
#define I2C_SCAN_TIMEOUT_MS         100     // Timeout for I2C device detection
#define SENSOR_WARMUP_MS            1000    // Sensor warmup time
#define CO2_WARMUP_MS               2000    // CO2 sensor needs longer warmup

// =============================================================================
// SERIAL MODES
// =============================================================================
typedef enum {
    SERIAL_MODE_OFF = 0,
    SERIAL_MODE_RS232,
    SERIAL_MODE_RS485_TX,
    SERIAL_MODE_RS485_RX,
    SERIAL_MODE_SDI12_TX,
    SERIAL_MODE_SDI12_RX
} SerialMode_t;

// =============================================================================
// TEST RESULTS
// =============================================================================
typedef enum {
    TEST_NOT_RUN = 0,
    TEST_PASS,
    TEST_FAIL,
    TEST_SKIP,
    TEST_WARN
} TestResult_t;

typedef struct {
    const char* name;
    TestResult_t result;
    char message[64];
} TestStatus_t;

#endif // WALTER_FEELS_CONFIG_H
