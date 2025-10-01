// Mock version of RA8875.h library for unit testing
#ifndef RA8875_h
#define RA8875_h

#include <stdint.h>

// Color constants
#define RA8875_BLACK       0x0000
#define RA8875_BLUE        0x001F
#define RA8875_RED         0xF800
#define RA8875_GREEN       0x07E0
#define RA8875_CYAN        0x07FF
#define RA8875_MAGENTA     0xF81F
#define RA8875_YELLOW      0xFFE0
#define RA8875_WHITE       0xFFFF

// Display size constants
#define RA8875_800x480     0x01

// Font size enum
enum RA8875tsize {
    RA8875_FONT_SIZE_X1 = 0,
    RA8875_FONT_SIZE_X2 = 1,
    RA8875_FONT_SIZE_X3 = 2,
    RA8875_FONT_SIZE_X4 = 3
};

class RA8875 {
public:
    // Constructor
    RA8875(uint8_t cs, uint8_t rst);

    // Display initialization and configuration
    bool begin(uint8_t display_size, uint8_t color_bpp = 16, uint32_t spi_clock = 20000000UL, uint32_t spi_clock_read = 4000000UL);
    void setRotation(uint8_t rotation);

    // Screen operations
    void clearScreen(uint16_t color = RA8875_BLACK);
    void fillWindow(uint16_t color = RA8875_BLACK);

    // Drawing operations
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    // Text operations
    void setTextColor(uint16_t color);
    void setCursor(uint16_t x, uint16_t y);
    void setFontScale(uint8_t scale);
    void setFontScale(enum RA8875tsize scale);
    void print(const char* text);
    void print(int value);
    void print(float value);
    uint8_t getFontWidth();

    // Graphics operations
    uint16_t Color24To565(uint32_t color24);
    void drawPixels(uint16_t* pixels, uint16_t count, uint16_t x, uint16_t y);

private:
    uint8_t _cs;
    uint8_t _rst;
    uint8_t _font_scale;
    uint16_t _cursor_x;
    uint16_t _cursor_y;
    uint16_t _text_color;
};

#endif