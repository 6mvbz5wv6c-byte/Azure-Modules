/**
 * @file ssd1306.cpp
 * @brief Minimal SSD1306 OLED Display Driver Implementation
 */

#include "ssd1306.h"
#include <stdarg.h>

// 5x7 font for basic ASCII (32-127)
static const uint8_t font5x7[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // Backslash
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ~
    0x08, 0x1C, 0x2A, 0x08, 0x08, // DEL (arrow)
};

// SSD1306 initialization sequence
static const uint8_t initCmd[] PROGMEM = {
    0xAE,        // Display off
    0xD5, 0x80,  // Set display clock div
    0xA8, 0x3F,  // Set multiplex ratio (64-1)
    0xD3, 0x00,  // Set display offset
    0x40,        // Set start line
    0x8D, 0x14,  // Charge pump on
    0x20, 0x00,  // Memory mode: horizontal
    0xA1,        // Segment remap
    0xC8,        // COM scan direction
    0xDA, 0x12,  // COM pins hardware config
    0x81, 0xCF,  // Set contrast
    0xD9, 0xF1,  // Set precharge
    0xDB, 0x40,  // Set VCOMH deselect
    0xA4,        // Entire display on/off
    0xA6,        // Normal display (not inverted)
    0xAF         // Display on
};

SSD1306::SSD1306(TwoWire& wire, uint8_t addr)
    : _wire(wire), _addr(addr), _initialized(false),
      _cursorX(0), _cursorY(0), _textSize(1) {
    memset(_buffer, 0, sizeof(_buffer));
}

bool SSD1306::begin() {
    if (!isConnected()) {
        return false;
    }

    // Send initialization commands
    for (size_t i = 0; i < sizeof(initCmd); i++) {
        _sendCommand(pgm_read_byte(&initCmd[i]));
    }

    clear();
    display();

    _initialized = true;
    return true;
}

bool SSD1306::isConnected() {
    _wire.beginTransmission(_addr);
    return (_wire.endTransmission() == 0);
}

void SSD1306::_sendCommand(uint8_t cmd) {
    _wire.beginTransmission(_addr);
    _wire.write(0x00);  // Command mode
    _wire.write(cmd);
    _wire.endTransmission();
}

void SSD1306::_sendCommands(const uint8_t* cmds, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        _sendCommand(cmds[i]);
    }
}

void SSD1306::clear() {
    memset(_buffer, 0, sizeof(_buffer));
    _cursorX = 0;
    _cursorY = 0;
}

void SSD1306::display() {
    // Set column address
    _sendCommand(0x21);
    _sendCommand(0);
    _sendCommand(SSD1306_WIDTH - 1);

    // Set page address
    _sendCommand(0x22);
    _sendCommand(0);
    _sendCommand((SSD1306_HEIGHT / 8) - 1);

    // Send buffer in chunks
    for (uint16_t i = 0; i < sizeof(_buffer); i += 16) {
        _wire.beginTransmission(_addr);
        _wire.write(0x40);  // Data mode
        for (uint8_t j = 0; j < 16 && (i + j) < sizeof(_buffer); j++) {
            _wire.write(_buffer[i + j]);
        }
        _wire.endTransmission();
    }
}

void SSD1306::displayOn() {
    _sendCommand(0xAF);
}

void SSD1306::displayOff() {
    _sendCommand(0xAE);
}

void SSD1306::setPixel(int16_t x, int16_t y, bool color) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    uint16_t idx = x + (y / 8) * SSD1306_WIDTH;
    uint8_t bit = 1 << (y % 8);

    if (color) {
        _buffer[idx] |= bit;
    } else {
        _buffer[idx] &= ~bit;
    }
}

void SSD1306::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    int16_t dx = abs(x1 - x0);
    int16_t dy = -abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        setPixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void SSD1306::drawRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    drawLine(x, y, x + w - 1, y);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1);
    drawLine(x + w - 1, y + h - 1, x, y + h - 1);
    drawLine(x, y + h - 1, x, y);
}

void SSD1306::fillRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    for (int16_t i = x; i < x + w; i++) {
        for (int16_t j = y; j < y + h; j++) {
            setPixel(i, j);
        }
    }
}

void SSD1306::setCursor(int16_t x, int16_t y) {
    _cursorX = x;
    _cursorY = y;
}

void SSD1306::setTextSize(uint8_t size) {
    _textSize = (size > 0) ? size : 1;
}

void SSD1306::_drawChar(int16_t x, int16_t y, char c, uint8_t size) {
    if (c < 32 || c > 127) c = '?';

    uint8_t idx = c - 32;
    const uint8_t* charData = &font5x7[idx * 5];

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = pgm_read_byte(&charData[col]);
        for (uint8_t row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                if (size == 1) {
                    setPixel(x + col, y + row);
                } else {
                    fillRect(x + col * size, y + row * size, size, size);
                }
            }
        }
    }
}

void SSD1306::print(char c) {
    if (c == '\n') {
        _cursorX = 0;
        _cursorY += 8 * _textSize;
        return;
    }

    if (c == '\r') {
        _cursorX = 0;
        return;
    }

    _drawChar(_cursorX, _cursorY, c, _textSize);
    _cursorX += 6 * _textSize;

    if (_cursorX + 6 * _textSize > SSD1306_WIDTH) {
        _cursorX = 0;
        _cursorY += 8 * _textSize;
    }
}

void SSD1306::print(const char* str) {
    while (*str) {
        print(*str++);
    }
}

void SSD1306::println(const char* str) {
    print(str);
    print('\n');
}

void SSD1306::printf(const char* format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    print(buf);
}

void SSD1306::showTestResult(const char* name, bool passed, const char* detail) {
    clear();
    setTextSize(1);

    setCursor(0, 0);
    print("TEST: ");
    println(name);

    setCursor(0, 16);
    setTextSize(2);
    if (passed) {
        println("PASS");
    } else {
        println("FAIL");
    }

    if (detail) {
        setTextSize(1);
        setCursor(0, 40);
        print(detail);
    }

    display();
}

void SSD1306::showProgress(const char* message, int current, int total) {
    clear();
    setTextSize(1);

    setCursor(0, 0);
    println(message);

    // Draw progress bar
    int16_t barX = 0;
    int16_t barY = 24;
    int16_t barW = SSD1306_WIDTH - 4;
    int16_t barH = 16;

    drawRect(barX, barY, barW, barH);

    int16_t fillW = (current * (barW - 4)) / total;
    fillRect(barX + 2, barY + 2, fillW, barH - 4);

    setCursor(0, 48);
    printf("%d / %d", current, total);

    display();
}

void SSD1306::showStatus(const char* line1, const char* line2,
                         const char* line3, const char* line4) {
    clear();
    setTextSize(1);

    if (line1) { setCursor(0, 0);  print(line1); }
    if (line2) { setCursor(0, 16); print(line2); }
    if (line3) { setCursor(0, 32); print(line3); }
    if (line4) { setCursor(0, 48); print(line4); }

    display();
}

void SSD1306::setContrast(uint8_t value) {
    _sendCommand(0x81);
    _sendCommand(value);
}

void SSD1306::invertDisplay(bool invert) {
    _sendCommand(invert ? 0xA7 : 0xA6);
}
