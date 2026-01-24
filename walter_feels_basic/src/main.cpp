/**
 * Walter Feels Basic Test
 *
 * Tests I2C bus and SSD1306 OLED display only.
 *
 * Walter Feels I2C pins:
 *   SDA = GPIO 42
 *   SCL = GPIO 2
 *   I2C Bus Power = GPIO 1
 *
 * OLED: SSD1306 128x64 at address 0x3C
 */

#include <Arduino.h>
#include <Wire.h>

// Walter Feels I2C pins
#define I2C_SDA       42
#define I2C_SCL       2
#define I2C_BUS_PWR   1

// OLED address (0x3C = 7-bit, same as 0x78 in 8-bit notation)
#define OLED_ADDR     0x3C

void scanI2C() {
    Serial.println("\n--- I2C Bus Scan ---");
    int found = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x3C) Serial.print(" (OLED SSD1306)");
            if (addr == 0x40) Serial.print(" (HDC1080 Temp/Humidity)");
            if (addr == 0x5C) Serial.print(" (LPS22HB Pressure)");
            if (addr == 0x68) Serial.print(" (LTC4015 Charger)");
            Serial.println();
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No devices found!");
    } else {
        Serial.printf("  Total: %d devices\n", found);
    }
    Serial.println("--------------------\n");
}

bool initOLED() {
    // Check if OLED responds
    Wire.beginTransmission(OLED_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("OLED not found at 0x3C");
        return false;
    }

    // SSD1306 init sequence
    const uint8_t initCmds[] = {
        0xAE,        // Display off
        0xD5, 0x80,  // Clock div
        0xA8, 0x3F,  // Multiplex (64-1)
        0xD3, 0x00,  // Display offset
        0x40,        // Start line
        0x8D, 0x14,  // Charge pump on
        0x20, 0x00,  // Memory mode horizontal
        0xA1,        // Segment remap
        0xC8,        // COM scan direction
        0xDA, 0x12,  // COM pins
        0x81, 0xCF,  // Contrast
        0xD9, 0xF1,  // Precharge
        0xDB, 0x40,  // VCOMH
        0xA4,        // Display from RAM
        0xA6,        // Normal (not inverted)
        0xAF         // Display on
    };

    for (size_t i = 0; i < sizeof(initCmds); i++) {
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x00);  // Command mode
        Wire.write(initCmds[i]);
        Wire.endTransmission();
    }

    Serial.println("OLED initialized");
    return true;
}

void clearOLED() {
    // Set column and page address
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(0x21); Wire.endTransmission();
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(0); Wire.endTransmission();
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(127); Wire.endTransmission();

    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(0x22); Wire.endTransmission();
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(0); Wire.endTransmission();
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);
    Wire.write(7); Wire.endTransmission();

    // Clear all pixels (128x64 = 1024 bytes)
    for (int i = 0; i < 1024; i += 16) {
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x40);  // Data mode
        for (int j = 0; j < 16; j++) {
            Wire.write(0x00);
        }
        Wire.endTransmission();
    }
}

void drawPattern() {
    // Draw a simple checkerboard pattern
    for (int page = 0; page < 8; page++) {
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x00);
        Wire.write(0xB0 + page);  // Set page
        Wire.endTransmission();

        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x00);
        Wire.write(0x00);  // Column low
        Wire.endTransmission();

        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x00);
        Wire.write(0x10);  // Column high
        Wire.endTransmission();

        for (int col = 0; col < 128; col += 16) {
            Wire.beginTransmission(OLED_ADDR);
            Wire.write(0x40);  // Data mode
            for (int j = 0; j < 16; j++) {
                Wire.write((page + col/8) % 2 ? 0xAA : 0x55);
            }
            Wire.endTransmission();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC

    Serial.println("\n\n=== Walter Feels Basic Test ===\n");

    // Enable I2C bus power
    pinMode(I2C_BUS_PWR, OUTPUT);
    digitalWrite(I2C_BUS_PWR, HIGH);
    Serial.println("I2C bus power enabled (GPIO 1 = HIGH)");
    delay(100);

    // Initialize I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.printf("I2C initialized: SDA=%d, SCL=%d\n", I2C_SDA, I2C_SCL);

    // Scan I2C bus
    scanI2C();

    // Initialize and test OLED
    if (initOLED()) {
        Serial.println("Clearing OLED...");
        clearOLED();
        delay(500);

        Serial.println("Drawing test pattern...");
        drawPattern();
        Serial.println("OLED test complete - you should see a checkerboard pattern");
    }

    Serial.println("\n=== Test Complete ===");
    Serial.println("Press 's' to scan I2C again");
    Serial.println("Press 'c' to clear OLED");
    Serial.println("Press 'p' to draw pattern");
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 's': scanI2C(); break;
            case 'c': clearOLED(); Serial.println("Cleared"); break;
            case 'p': drawPattern(); Serial.println("Pattern drawn"); break;
        }
    }
    delay(10);
}
