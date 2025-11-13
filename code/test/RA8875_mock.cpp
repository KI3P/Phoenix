#include "RA8875.h"
#include "Arduino.h"
#include <iostream>
#include <iomanip>

RA8875::RA8875(uint8_t cs, uint8_t rst) : _cs(cs), _rst(rst), _font_scale(1), _cursor_x(0), _cursor_y(0), _text_color(RA8875_WHITE) {
}

bool RA8875::begin(uint8_t display_size, uint8_t color_bpp, uint32_t spi_clock, uint32_t spi_clock_read) {
    // Mock implementation - always returns success
    return true;
}

void RA8875::setRotation(uint8_t rotation) {
    // Mock implementation - store rotation value
}

void RA8875::clearScreen(uint16_t color) {
    // Mock implementation - could log the action if needed
}

void RA8875::fillWindow(uint16_t color) {
    // Mock implementation - fills entire window
}

void RA8875::setTextColor(uint16_t color) {
    _text_color = color;
}

void RA8875::setTextColor(uint16_t color1, uint16_t color2) {
    _text_color = color1;
}

void RA8875::setCursor(uint16_t x, uint16_t y) {
    _cursor_x = x;
    _cursor_y = y;
}

void RA8875::setFontScale(uint8_t scale) {
    _font_scale = scale;
}

void RA8875::setFontScale(enum RA8875tsize scale) {
    _font_scale = static_cast<uint8_t>(scale);
}

void RA8875::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    // Mock implementation - could log the action if needed for testing
}

void RA8875::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    // Mock implementation - could log the action if needed for testing
}

void RA8875::drawCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color) {
    // Mock implementation - could log the action if needed for testing
}

void RA8875::fillCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color) {
    // Mock implementation - could log the action if needed for testing
}

void RA8875::setFont(const void* font) {
    // Mock implementation - store font pointer if needed
}

void RA8875::print(const char* text) {
    // Mock implementation - could log the text if needed for testing
}

void RA8875::print(const String& str) {
    // Mock implementation - could log the text if needed for testing
}

void RA8875::print(int value) {
    // Mock implementation - could log the value if needed for testing
}

void RA8875::print(int64_t value) {
    // Mock implementation - could log the value if needed for testing
}

void RA8875::print(float value) {
    // Mock implementation - could log the value if needed for testing
}

void RA8875::setFontDefault() {
    // Mock implementation - reset to default font
    _font_scale = 1;
}

uint8_t RA8875::getFontWidth() {
    // Return a reasonable mock font width based on scale
    return 8 * _font_scale;
}

uint8_t RA8875::getFontHeight() {
    // Return a reasonable mock font height based on scale
    return 16 * _font_scale;
}

uint16_t RA8875::Color24To565(uint32_t color24) {
    // Convert 24-bit RGB to 16-bit RGB565 format
    uint8_t r = (color24 >> 16) & 0xFF;
    uint8_t g = (color24 >> 8) & 0xFF;
    uint8_t b = color24 & 0xFF;

    // Convert to 5-6-5 format
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;

    return (r5 << 11) | (g6 << 5) | b5;
}

void RA8875::drawPixels(uint16_t* pixels, uint16_t count, uint16_t x, uint16_t y) {
    // Mock implementation - could log the pixel drawing if needed for testing
}

void RA8875::drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    // Mock implementation - could log the line drawing if needed for testing
}

void RA8875::drawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
    // Mock implementation - draws a vertical line
}

void RA8875::drawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    // Mock implementation - draws a horizontal line
}

void RA8875::useLayers(bool enable) {
    // Mock implementation - enable/disable layer support
}

void RA8875::layerEffect(uint8_t effect) {
    // Mock implementation - set layer blending effect
}

void RA8875::writeTo(uint8_t layer) {
    // Mock implementation - select which layer to write to
}

void RA8875::clearMemory() {
    // Mock implementation - clear display memory
}

void RA8875::BTE_move(uint16_t src_x, uint16_t src_y, uint16_t width, uint16_t height,
                      uint16_t dst_x, uint16_t dst_y, uint8_t rop, uint8_t bte_operation) {
    // Mock implementation - Block Transfer Engine memory move operation
    // Used for scrolling waterfall display
}

bool RA8875::readStatus() {
    // Mock implementation - return false to indicate operation complete
    return false;
}

void RA8875::writeRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data) {
    // Mock implementation - write a rectangular array of pixels
}