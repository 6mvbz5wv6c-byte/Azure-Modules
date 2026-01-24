/**
 * @file ssd1306.h
 * @brief Minimal SSD1306 OLED Display Driver
 *
 * Lightweight driver for 128x64 SSD1306 OLED displays over I2C.
 * Includes basic text rendering with a 5x7 font.
 */

#ifndef SSD1306_H
#define SSD1306_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

class SSD1306 {
public:
    SSD1306(TwoWire& wire = Wire, uint8_t addr = SSD1306_I2C_ADDR);

    bool begin();
    bool isConnected();

    void clear();
    void display();
    void displayOn();
    void displayOff();

    // Drawing primitives
    void setPixel(int16_t x, int16_t y, bool color = true);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h);

    // Text rendering
    void setCursor(int16_t x, int16_t y);
    void setTextSize(uint8_t size);
    void print(const char* str);
    void print(char c);
    void println(const char* str = "");
    void printf(const char* format, ...);

    // Convenience methods
    void showTestResult(const char* name, bool passed, const char* detail = nullptr);
    void showProgress(const char* message, int current, int total);
    void showStatus(const char* line1, const char* line2 = nullptr,
                   const char* line3 = nullptr, const char* line4 = nullptr);

    // Contrast and invert
    void setContrast(uint8_t value);
    void invertDisplay(bool invert);

private:
    TwoWire& _wire;
    uint8_t _addr;
    bool _initialized;

    uint8_t _buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];
    int16_t _cursorX;
    int16_t _cursorY;
    uint8_t _textSize;

    void _sendCommand(uint8_t cmd);
    void _sendCommands(const uint8_t* cmds, uint8_t len);
    void _drawChar(int16_t x, int16_t y, char c, uint8_t size);
};

#endif // SSD1306_H
