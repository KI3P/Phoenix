// SDL2-based graphical display backend for RA8875 mock
// Provides visual rendering of the TFT display for testing/simulation
// Cross-platform: Linux, Windows, macOS

#include "RA8875.h"
#include "Arduino.h"
#include "RFBoard.h"
#include "OpenAudio_ArduinoLibrary.h"
#include <iostream>
#include <cstring>
#include <cmath>

#ifdef USE_SDL_DISPLAY
#include <SDL2/SDL.h>

// Display dimensions
static const int DISPLAY_WIDTH = 800;
static const int DISPLAY_HEIGHT = 480;
static const int REGISTER_PANEL_HEIGHT = 60;
static const int HELP_PANEL_HEIGHT = 200;
static const int SDL_WINDOW_HEIGHT = DISPLAY_HEIGHT + REGISTER_PANEL_HEIGHT + HELP_PANEL_HEIGHT;

// SDL globals
static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static SDL_Texture* g_texture = nullptr;
static SDL_Texture* g_help_texture = nullptr;
static SDL_Texture* g_register_texture = nullptr;
static uint32_t* g_framebuffer = nullptr;  // ARGB8888 format
static uint32_t* g_help_buffer = nullptr;  // Help panel buffer
static uint32_t* g_register_buffer = nullptr;  // Hardware register panel buffer
static bool g_initialized = false;
static bool g_layers_enabled = false;
static uint8_t g_current_layer = 0;
static uint32_t* g_layer1 = nullptr;
static uint32_t* g_layer2 = nullptr;
static uint8_t g_layer_effect = 0;  // OR, AND, TRANSPARENT

// Forward declaration
static void update_register_panel();
static void update_help_panel_audio_source();

// Convert RGB565 to ARGB8888
static uint32_t rgb565_to_argb8888(uint16_t color) {
    uint8_t r = ((color >> 11) & 0x1F) << 3;
    uint8_t g = ((color >> 5) & 0x3F) << 2;
    uint8_t b = (color & 0x1F) << 3;
    // Expand to full range
    r |= (r >> 5);
    g |= (g >> 6);
    b |= (b >> 5);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Get pointer to current layer buffer
static uint32_t* get_current_buffer() {
    if (g_layers_enabled && g_current_layer == 1 && g_layer2) {
        return g_layer2;
    }
    return g_layer1 ? g_layer1 : g_framebuffer;
}

// Composite layers and update display
static void update_display() {
    if (!g_initialized || !g_framebuffer) return;

    if (g_layers_enabled && g_layer1 && g_layer2) {
        // Composite layers based on effect
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            uint32_t c1 = g_layer1[i];
            uint32_t c2 = g_layer2[i];

            switch (g_layer_effect) {
                case 0:  // OR
                    g_framebuffer[i] = c1 | c2;
                    break;
                case 1:  // AND
                    g_framebuffer[i] = c1 & c2;
                    break;
                case 2:  // TRANSPARENT (layer 2 on top where not black)
                    g_framebuffer[i] = (c2 != 0xFF000000) ? c2 : c1;
                    break;
                default:
                    g_framebuffer[i] = c1;
                    break;
            }
        }
    } else if (g_layer1 && g_layer1 != g_framebuffer) {
        memcpy(g_framebuffer, g_layer1, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint32_t));
    }

    // Update register panel with current hardware state
    update_register_panel();

    // Update audio source indicator in help panel
    update_help_panel_audio_source();

    SDL_UpdateTexture(g_texture, nullptr, g_framebuffer, DISPLAY_WIDTH * sizeof(uint32_t));
    SDL_UpdateTexture(g_register_texture, nullptr, g_register_buffer, DISPLAY_WIDTH * sizeof(uint32_t));
    SDL_UpdateTexture(g_help_texture, nullptr, g_help_buffer, DISPLAY_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(g_renderer);

    // Render main display at top
    SDL_Rect display_rect = {0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT};
    SDL_RenderCopy(g_renderer, g_texture, nullptr, &display_rect);

    // Render register panel in middle
    SDL_Rect register_rect = {0, DISPLAY_HEIGHT, DISPLAY_WIDTH, REGISTER_PANEL_HEIGHT};
    SDL_RenderCopy(g_renderer, g_register_texture, nullptr, &register_rect);

    // Render help panel at bottom
    SDL_Rect help_rect = {0, DISPLAY_HEIGHT + REGISTER_PANEL_HEIGHT, DISPLAY_WIDTH, HELP_PANEL_HEIGHT};
    SDL_RenderCopy(g_renderer, g_help_texture, nullptr, &help_rect);

    SDL_RenderPresent(g_renderer);
    // Note: Don't poll events here - let the main loop's processEvents() handle them
}

// Set a pixel in the current buffer
static void set_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    uint32_t* buffer = get_current_buffer();
    if (buffer) {
        buffer[y * DISPLAY_WIDTH + x] = color;
    }
}

// Simple built-in font (8x16 pixel monospace)
// Basic ASCII characters 32-126
static const uint8_t font_8x16[][16] = {
    // Space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (33)
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    // " (34)
    {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // # (35)
    {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
    // $ (36)
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
    // % (37)
    {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
    // & (38)
    {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // ' (39)
    {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ( (40)
    {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
    // ) (41)
    {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
    // * (42)
    {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    // + (43)
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    // , (44)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
    // - (45)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // . (46)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    // / (47)
    {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
    // 0 (48)
    {0x00,0x00,0x38,0x6C,0xC6,0xC6,0xD6,0xD6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00},
    // 1 (49)
    {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    // 2 (50)
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
    // 3 (51)
    {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 4 (52)
    {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
    // 5 (53)
    {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 6 (54)
    {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 7 (55)
    {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    // 8 (56)
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 9 (57)
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    // : (58)
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    // ; (59)
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
    // < (60)
    {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
    // = (61)
    {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // > (62)
    {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
    // ? (63)
    {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    // @ (64)
    {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
    // A (65)
    {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // B (66)
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    // C (67)
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
    // D (68)
    {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    // E (69)
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    // F (70)
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // G (71)
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
    // H (72)
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // I (73)
    {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // J (74)
    {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    // K (75)
    {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    // L (76)
    {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    // M (77)
    {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // N (78)
    {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // O (79)
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // P (80)
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // Q (81)
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
    // R (82)
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    // S (83)
    {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // T (84)
    {0x00,0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // U (85)
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // V (86)
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    // W (87)
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00},
    // X (88)
    {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // Y (89)
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // Z (90)
    {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
    // [ (91)
    {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
    // \ (92)
    {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00},
    // ] (93)
    {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
    // ^ (94)
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // _ (95)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00},
    // ` (96)
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // a (97)
    {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // b (98)
    {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
    // c (99)
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // d (100)
    {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // e (101)
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // f (102)
    {0x00,0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // g (103)
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00},
    // h (104)
    {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    // i (105)
    {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // j (106)
    {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00},
    // k (107)
    {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    // l (108)
    {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // m (109)
    {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
    // n (110)
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    // o (111)
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // p (112)
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    // q (113)
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00},
    // r (114)
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // s (115)
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // t (116)
    {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
    // u (117)
    {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // v (118)
    {0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},
    // w (119)
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
    // x (120)
    {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    // y (121)
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00},
    // z (122)
    {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
    // { (123)
    {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
    // | (124)
    {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    // } (125)
    {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
    // ~ (126)
    {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// Set a pixel in the help buffer
static void set_help_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= HELP_PANEL_HEIGHT) return;
    if (g_help_buffer) {
        g_help_buffer[y * DISPLAY_WIDTH + x] = color;
    }
}

// Draw a single character to the help buffer using built-in font
static int draw_help_char(int x, int y, char c, uint32_t color, int scale = 1) {
    if (c < 32 || c > 126) c = '?';
    int idx = c - 32;

    int char_width = 8 * scale;

    for (int row = 0; row < 16; row++) {
        uint8_t line = font_8x16[idx][row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        set_help_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }

    return char_width;
}

// Draw a string to the help buffer
static void draw_help_string(int x, int y, const char* text, uint32_t color, int scale = 1) {
    while (*text) {
        x += draw_help_char(x, y, *text, color, scale);
        text++;
    }
}

// Set a pixel in the register panel buffer
static void set_register_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= REGISTER_PANEL_HEIGHT) return;
    if (g_register_buffer) {
        g_register_buffer[y * DISPLAY_WIDTH + x] = color;
    }
}

// Draw a single character to the register panel buffer using built-in font
static int draw_register_char(int x, int y, char c, uint32_t color, int scale = 1) {
    if (c < 32 || c > 126) c = '?';
    int idx = c - 32;

    int char_width = 8 * scale;

    for (int row = 0; row < 16; row++) {
        uint8_t line = font_8x16[idx][row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        set_register_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }

    return char_width;
}

// Draw a string to the register panel buffer
static void draw_register_string(int x, int y, const char* text, uint32_t color, int scale = 1) {
    while (*text) {
        x += draw_register_char(x, y, *text, color, scale);
        text++;
    }
}

// Initialize the help panel with keyboard map
static void init_help_panel() {
    if (!g_help_buffer) return;

    // Fill with dark gray background
    uint32_t bg_color = 0xFF202020;
    uint32_t title_color = 0xFFFFFF00;  // Yellow
    uint32_t key_color = 0xFF00FF00;    // Green
    uint32_t desc_color = 0xFFCCCCCC;   // Light gray
    uint32_t separator_color = 0xFF404040;

    for (int i = 0; i < DISPLAY_WIDTH * HELP_PANEL_HEIGHT; i++) {
        g_help_buffer[i] = bg_color;
    }

    // Draw separator line at top
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        g_help_buffer[x] = separator_color;
    }

    int y = 8;
    int col1 = 10;
    int col2 = 210;
    int col3 = 410;
    int col4 = 610;
    int line_height = 16;

    // Title
    draw_help_string(col1, y, "KEYBOARD CONTROLS", title_color);
    y += line_height + 4;

    // Column headers
    draw_help_string(col1, y, "Front Panel (Top)", title_color);
    draw_help_string(col2, y, "Front Panel (Bot)", title_color);
    draw_help_string(col3, y, "Encoders", title_color);
    draw_help_string(col4, y, "PTT/CW/Audio", title_color);
    y += line_height + 2;

    // Row 1
    draw_help_string(col1, y, "1", key_color); draw_help_string(col1 + 16, y, "Menu Select (0)", desc_color);
    draw_help_string(col2, y, "Q", key_color); draw_help_string(col2 + 16, y, "Noise Reduc (9)", desc_color);
    draw_help_string(col3, y, "Up/Dn", key_color); draw_help_string(col3 + 48, y, "Filter Enc", desc_color);
    draw_help_string(col4, y, "P", key_color); draw_help_string(col4 + 16, y, "PTT (hold)", desc_color);
    y += line_height;

    // Row 2
    draw_help_string(col1, y, "2", key_color); draw_help_string(col1 + 16, y, "Main Menu (1)", desc_color);
    draw_help_string(col2, y, "W", key_color); draw_help_string(col2 + 16, y, "Notch (10)", desc_color);
    draw_help_string(col3, y, "+/-", key_color); draw_help_string(col3 + 32, y, "Volume Enc", desc_color);
    draw_help_string(col4, y, ".", key_color); draw_help_string(col4 + 16, y, "CW Key 1", desc_color);
    y += line_height;

    // Row 3
    draw_help_string(col1, y, "3", key_color); draw_help_string(col1 + 16, y, "Band Up (2)", desc_color);
    draw_help_string(col2, y, "E", key_color); draw_help_string(col2 + 16, y, "Fine Tune+ (11)", desc_color);
    draw_help_string(col3, y, "[ / ]", key_color); draw_help_string(col3 + 48, y, "Main Tune", desc_color);
    draw_help_string(col4, y, "/", key_color); draw_help_string(col4 + 16, y, "CW Key 2", desc_color);
    y += line_height;

    // Row 4
    draw_help_string(col1, y, "4", key_color); draw_help_string(col1 + 16, y, "Zoom (3)", desc_color);
    draw_help_string(col2, y, "R", key_color); draw_help_string(col2 + 16, y, "Filter (12)", desc_color);
    draw_help_string(col3, y, "; / '", key_color); draw_help_string(col3 + 48, y, "Fine Tune", desc_color);
    draw_help_string(col4, y, "A", key_color); draw_help_string(col4 + 16, y, "Audio Source", desc_color);
    y += line_height;

    // Row 5
    draw_help_string(col1, y, "5", key_color); draw_help_string(col1 + 16, y, "Reset Tune (4)", desc_color);
    draw_help_string(col2, y, "T", key_color); draw_help_string(col2 + 16, y, "Decoder (13)", desc_color);
    y += line_height;

    // Row 6
    draw_help_string(col1, y, "6", key_color); draw_help_string(col1 + 16, y, "Band Down (5)", desc_color);
    draw_help_string(col2, y, "Y", key_color); draw_help_string(col2 + 16, y, "DFE (14)", desc_color);
    draw_help_string(col3, y, "Additional Buttons:", title_color);
    y += line_height;

    // Row 7
    draw_help_string(col1, y, "7", key_color); draw_help_string(col1 + 16, y, "Toggle Mode (6)", desc_color);
    draw_help_string(col2, y, "U", key_color); draw_help_string(col2 + 16, y, "Bearing (15)", desc_color);
    draw_help_string(col3, y, "V", key_color); draw_help_string(col3 + 16, y, "Vol (18)", desc_color);
    draw_help_string(col3 + 100, y, "F", key_color); draw_help_string(col3 + 116, y, "Filter (19)", desc_color);
    y += line_height;

    // Row 8
    draw_help_string(col1, y, "8", key_color); draw_help_string(col1 + 16, y, "Demod (7)", desc_color);
    draw_help_string(col2, y, "O", key_color); draw_help_string(col2 + 16, y, "Spare (16)", desc_color);
    draw_help_string(col3, y, "N", key_color); draw_help_string(col3 + 16, y, "FT (20)", desc_color);
    draw_help_string(col3 + 100, y, "B", key_color); draw_help_string(col3 + 116, y, "VFO (21)", desc_color);
    y += line_height;

    // Row 9
    draw_help_string(col1, y, "9", key_color); draw_help_string(col1 + 16, y, "Main Tune+ (8)", desc_color);
    draw_help_string(col2, y, "H", key_color); draw_help_string(col2 + 16, y, "Home (17)", desc_color);
    draw_help_string(col4, y, "ESC", key_color); draw_help_string(col4 + 32, y, "Exit", desc_color);
}

// Update the audio source indicator in the help panel
// This is called each frame to show the current audio source
static void update_help_panel_audio_source() {
    if (!g_help_buffer) return;

    // Position for audio source indicator (prominent location)
    int x_start = 350;  // 550 - 200 (moved left by 1/4 screen width)
    int y_pos = 8;  // Same line as title
    int box_width = 370;  // Wide enough for "Audio Source: Two-Tone (700/1900Hz @ 48kHz)"
    int box_height = 20;

    // Colors
    uint32_t bg_color = 0xFF303030;       // Slightly lighter than panel bg
    uint32_t border_color = 0xFF505050;   // Border
    uint32_t label_color = 0xFFCCCCCC;    // Light gray for label
    uint32_t source_color = 0xFF00FFFF;   // Cyan for source name (stands out)

    // Clear the area with background
    for (int py = y_pos; py < y_pos + box_height && py < HELP_PANEL_HEIGHT; py++) {
        for (int px = x_start; px < x_start + box_width && px < DISPLAY_WIDTH; px++) {
            g_help_buffer[py * DISPLAY_WIDTH + px] = bg_color;
        }
    }

    // Draw border
    for (int px = x_start; px < x_start + box_width && px < DISPLAY_WIDTH; px++) {
        g_help_buffer[y_pos * DISPLAY_WIDTH + px] = border_color;
        g_help_buffer[(y_pos + box_height - 1) * DISPLAY_WIDTH + px] = border_color;
    }
    for (int py = y_pos; py < y_pos + box_height && py < HELP_PANEL_HEIGHT; py++) {
        g_help_buffer[py * DISPLAY_WIDTH + x_start] = border_color;
        g_help_buffer[py * DISPLAY_WIDTH + x_start + box_width - 1] = border_color;
    }

    // Draw label and current source
    draw_help_string(x_start + 5, y_pos + 2, "Audio Source:", label_color);

    // Get current audio source name
    const char* source_name = getAudioInputSourceName();
    draw_help_string(x_start + 115, y_pos + 2, source_name, source_color);
}

// Initialize the hardware register panel (static layout)
static void init_register_panel() {
    if (!g_register_buffer) return;

    // Fill with dark blue-gray background
    uint32_t bg_color = 0xFF1a1a2e;
    for (int i = 0; i < DISPLAY_WIDTH * REGISTER_PANEL_HEIGHT; i++) {
        g_register_buffer[i] = bg_color;
    }

    // Draw separator lines at top and bottom
    uint32_t separator_color = 0xFF404060;
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        g_register_buffer[x] = separator_color;
        g_register_buffer[(REGISTER_PANEL_HEIGHT - 1) * DISPLAY_WIDTH + x] = separator_color;
    }
}

// External reference to hardware register (defined in Globals.cpp)
extern uint64_t hardwareRegister;

// Update the hardware register panel with current state
static void update_register_panel() {
    if (!g_register_buffer) return;

    // Color scheme
    uint32_t bg_color = 0xFF1a1a2e;       // Dark blue-gray background
    uint32_t title_color = 0xFFFFFF00;    // Yellow for titles
    uint32_t on_color = 0xFF00FF00;       // Green for 1/ON bits
    uint32_t off_color = 0xFF444444;      // Dark gray for 0/OFF bits
    uint32_t label_color = 0xFFAAAAAA;    // Light gray for labels
    uint32_t value_color = 0xFFFFFFFF;    // White for hex value
    uint32_t separator_color = 0xFF404060;

    // Clear buffer
    for (int i = 0; i < DISPLAY_WIDTH * REGISTER_PANEL_HEIGHT; i++) {
        g_register_buffer[i] = bg_color;
    }

    // Draw separator lines
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        g_register_buffer[x] = separator_color;
        g_register_buffer[(REGISTER_PANEL_HEIGHT - 1) * DISPLAY_WIDTH + x] = separator_color;
    }

    int y = 4;
    int line_height = 18;

    // Row 1: Title and hex value
    draw_register_string(10, y, "HARDWARE REGISTER:", title_color);
    char hex_str[20];
    snprintf(hex_str, sizeof(hex_str), "0x%016lX", hardwareRegister);
    draw_register_string(170, y, hex_str, value_color);

    y += line_height;

    // Row 2: Section labels with bit ranges and individual bit names
    draw_register_string(10, y, "BPF", label_color);
    draw_register_string(55, y, "RXATT", label_color);
    draw_register_string(115, y, "TXATT", label_color);
    // RF bit labels: TXVFO(32) RXVFO(15), CWVFO(14), CAL(13), MODE(12), CW(11), RXTX(10)
    draw_register_string(192, y, "TV RV CV CA MO CW RT", label_color);
    // LPF bit labels: RXBPF(9), TXBPF(8), PA100W(7), XVTR(6), ANT1(5), ANT0(4), LPFBAND[3:0]
    draw_register_string(366, y, "RB TB PA XV A1 A0  LPF", label_color);

    y += line_height;

    // Row 3: Binary bits with visual indicators
    int x_pos = 10;

    // BPF[31:28] - 4 bits
    for (int bit = 31; bit >= 28; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
    }
    x_pos += 12;  // gap to align with RXATT label

    // RXATT[27:22] - 6 bits
    for (int bit = 27; bit >= 22; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
    }
    x_pos += 12;

    // TXATT[21:16] - 6 bits
    for (int bit = 21; bit >= 16; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
    }
    x_pos += 30;

    // TXVFO bit which is off on its own boo hoo
    uint32_t color = (hardwareRegister & (1UL << 32)) ? on_color : off_color;
    draw_register_char(x_pos, y, (hardwareRegister & (1UL << 32)) ? '1' : '0', color);
    x_pos += 8;
    x_pos += 16;

    // RF[15:10] - 6 bits (RXVFO, CWVFO, CAL, MODE, CW, RXTX)
    for (int bit = 15; bit >= 10; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
        x_pos += 16;  // extra spacing to align with labels
    }
    x_pos += 6;

    // LPF[9:0] - 10 bits (RXBPF, TXBPF, PA100W, XVTR, ANT1, ANT0, LPFBAND3-0)
    for (int bit = 9; bit >= 4; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
        x_pos += 16;  // extra spacing to align with labels
    }
    // LPFBAND[3:0] - 4 bits grouped together
    x_pos += 8;
    for (int bit = 3; bit >= 0; bit--) {
        uint32_t color = (hardwareRegister & (1UL << bit)) ? on_color : off_color;
        draw_register_char(x_pos, y, (hardwareRegister & (1UL << bit)) ? '1' : '0', color);
        x_pos += 8;
    }

    // VFO Frequency Display (right side of panel)
    int vfo_x = 560;  // Start position for VFO section
    int vfo_y = 4;

    // Row 1: VFO section title
    draw_register_string(vfo_x, vfo_y, "VFO FREQUENCIES", title_color);
    vfo_y += line_height;

    // Get current VFO frequencies (in Hz)
    int64_t rx_freq_hz = GetRXVFOFrequency();
    int64_t tx_freq_hz = GetTXVFOFrequency();
    int64_t cw_freq_hz = GetCWVFOFrequency();

    // Row 2: RX VFO
    draw_register_string(vfo_x, vfo_y, "RX:", label_color);
    char freq_str[20];
    // Format as MHz with 3 decimal places (kHz resolution)
    snprintf(freq_str, sizeof(freq_str), "%8.6f MHz", rx_freq_hz / 1000000.0);
    draw_register_string(vfo_x + 25, vfo_y, freq_str, value_color);
    vfo_y += line_height;

    // Row 3: TX and CW VFOs
    draw_register_string(vfo_x, vfo_y, "TX:", label_color);
    snprintf(freq_str, sizeof(freq_str), "%8.6f", tx_freq_hz / 1000000.0);
    draw_register_string(vfo_x + 25, vfo_y, freq_str, value_color);

    draw_register_string(vfo_x + 130, vfo_y, "CW:", label_color);
    snprintf(freq_str, sizeof(freq_str), "%8.6f", cw_freq_hz / 1000000.0);
    draw_register_string(vfo_x + 130+25, vfo_y, freq_str, value_color);
}

// Draw a single character using built-in font and return its width
static int draw_char_builtin(int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 126) c = '?';
    int idx = c - 32;

    int char_width = 8 * scale;

    for (int row = 0; row < 16; row++) {
        uint8_t line = font_8x16[idx][row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                // Draw scaled pixel
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        set_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }

    return char_width;
}

// Draw a single character using GFX font and return its xAdvance
// For GFX fonts: y is the baseline. yOffset is typically negative (chars draw above baseline)
// The RA8875 library interprets setCursor y as the TOP of the text area, not baseline
// So we add yAdvance to convert from top-of-text to baseline
static int draw_char_gfx(int x, int baseline_y, char c, uint32_t color, const GFXfont* font) {
    if (!font || !font->glyph || !font->bitmap) return 0;

    // Check if character is in font range
    if (c < font->first || c > font->last) {
        if (c == ' ') {
            // Return space width - use first glyph's xAdvance as estimate
            return font->glyph[0].xAdvance;
        }
        return 0;
    }

    int idx = c - font->first;
    const GFXglyph* glyph = &font->glyph[idx];

    // Get glyph bitmap data
    const uint8_t* bitmap = font->bitmap;
    uint16_t bo = glyph->bitmapOffset;
    uint8_t w = glyph->width;
    uint8_t h = glyph->height;
    int8_t xo = glyph->xOffset;
    int8_t yo = glyph->yOffset;

    // For empty glyphs (like space), just return advance
    if (w == 0 || h == 0) {
        return glyph->xAdvance;
    }

    // Draw the glyph bitmap
    // GFX fonts store bitmaps as packed bits, MSB first
    // yo is typically negative (e.g., -33), meaning char draws above baseline
    uint8_t bit = 0;
    uint8_t bits = 0;

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            if (!(bit & 7)) {
                bits = bitmap[bo++];
            }
            bit++;
            if (bits & 0x80) {
                int px = x + xo + xx;
                int py = baseline_y + yo + yy;
                set_pixel(px, py, color);
            }
            bits <<= 1;
        }
    }

    return glyph->xAdvance;
}

// RA8875 class implementation with SDL backend
RA8875::RA8875(uint8_t cs, uint8_t rst)
    : _cs(cs), _rst(rst), _font_scale(1), _cursor_x(0), _cursor_y(0), _text_color(RA8875_WHITE), _custom_font(nullptr) {
}

bool RA8875::begin(uint8_t display_size, uint8_t color_bpp, uint32_t spi_clock, uint32_t spi_clock_read) {
    if (g_initialized) return true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }

    g_window = SDL_CreateWindow(
        "Phoenix SDR Radio Simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        DISPLAY_WIDTH,
        SDL_WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!g_window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    // Note: Don't use SDL_RENDERER_PRESENTVSYNC - it limits framerate to display refresh rate
    // which is too slow for audio processing (need ~94 Hz for 48kHz audio at 512 samples/frame)
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    g_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT
    );

    if (!g_texture) {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    // Create help panel texture
    g_help_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH,
        HELP_PANEL_HEIGHT
    );

    if (!g_help_texture) {
        std::cerr << "Help texture creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyTexture(g_texture);
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    // Create register panel texture
    g_register_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH,
        REGISTER_PANEL_HEIGHT
    );

    if (!g_register_texture) {
        std::cerr << "Register texture creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyTexture(g_help_texture);
        SDL_DestroyTexture(g_texture);
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    // Allocate framebuffer and layer buffers
    g_framebuffer = new uint32_t[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    g_layer1 = new uint32_t[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    g_layer2 = new uint32_t[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    g_help_buffer = new uint32_t[DISPLAY_WIDTH * HELP_PANEL_HEIGHT];
    g_register_buffer = new uint32_t[DISPLAY_WIDTH * REGISTER_PANEL_HEIGHT];

    // Clear to black
    uint32_t black = 0xFF000000;
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        g_framebuffer[i] = black;
        g_layer1[i] = black;
        g_layer2[i] = black;
    }

    // Initialize help panel with keyboard map
    init_help_panel();

    // Initialize register panel
    init_register_panel();

    g_initialized = true;
    update_display();

    std::cout << "SDL Display initialized: " << DISPLAY_WIDTH << "x" << DISPLAY_HEIGHT << std::endl;
    return true;
}

void RA8875::setRotation(uint8_t rotation) {
    // Rotation not implemented in simulator
}

void RA8875::clearScreen(uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);
    uint32_t* buffer = get_current_buffer();

    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        buffer[i] = argb;
    }
    // Don't update_display() here - let the main loop do it once per frame
}

void RA8875::fillWindow(uint16_t color) {
    clearScreen(color);
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
    if (_font_scale < 1) _font_scale = 1;
    if (_font_scale > 4) _font_scale = 4;
}

void RA8875::setFontScale(enum RA8875tsize scale) {
    _font_scale = static_cast<uint8_t>(scale) + 1;
}

void RA8875::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++) {
        for (int px = x; px < x + w && px < DISPLAY_WIDTH; px++) {
            set_pixel(px, py, argb);
        }
    }
}

void RA8875::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    // Top and bottom edges
    for (int px = x; px < x + w && px < DISPLAY_WIDTH; px++) {
        set_pixel(px, y, argb);
        set_pixel(px, y + h - 1, argb);
    }

    // Left and right edges
    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++) {
        set_pixel(x, py, argb);
        set_pixel(x + w - 1, py, argb);
    }
}

void RA8875::drawCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    // Midpoint circle algorithm
    int cx = 0;
    int cy = r;
    int d = 1 - r;

    while (cx <= cy) {
        set_pixel(x + cx, y + cy, argb);
        set_pixel(x - cx, y + cy, argb);
        set_pixel(x + cx, y - cy, argb);
        set_pixel(x - cx, y - cy, argb);
        set_pixel(x + cy, y + cx, argb);
        set_pixel(x - cy, y + cx, argb);
        set_pixel(x + cy, y - cx, argb);
        set_pixel(x - cy, y - cx, argb);

        if (d < 0) {
            d += 2 * cx + 3;
        } else {
            d += 2 * (cx - cy) + 5;
            cy--;
        }
        cx++;
    }
}

void RA8875::fillCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    for (int py = -r; py <= r; py++) {
        int width = (int)sqrt(r * r - py * py);
        for (int px = -width; px <= width; px++) {
            set_pixel(x + px, y + py, argb);
        }
    }
}

void RA8875::setFont(const void* font) {
    _custom_font = font;
}

void RA8875::print(const char* text) {
    if (!g_initialized || !text) return;

    uint32_t argb = rgb565_to_argb8888(_text_color);

    while (*text) {
        if (*text == '\n') {
            _cursor_x = 0;
            _cursor_y += getFontHeight();
        } else {
            int char_width;
            if (_custom_font) {
                // Use GFX font
                // RA8875 library: cursor y is TOP of text area
                // GFX font: draw_char_gfx expects baseline y
                // Convert by adding approximately 45% of yAdvance
                const GFXfont* font = (const GFXfont*)_custom_font;
                int baseline_y = _cursor_y + (font->yAdvance * 45 / 100);
                char_width = draw_char_gfx(_cursor_x, baseline_y, *text, argb, font);
            } else {
                // Use built-in font - cursor y is top of character
                char_width = draw_char_builtin(_cursor_x, _cursor_y, *text, argb, _font_scale);
            }
            _cursor_x += char_width;
        }
        text++;
    }
}

void RA8875::print(const String& str) {
    print(str.c_str());
}

void RA8875::print(int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    print(buf);
}

void RA8875::print(int64_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)value);
    print(buf);
}

void RA8875::print(float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    print(buf);
}

void RA8875::setFontDefault() {
    _font_scale = 1;
    _custom_font = nullptr;
}

uint8_t RA8875::getFontWidth() {
    if (_custom_font) {
        // For GFX fonts, return average character width (use '0' as reference)
        const GFXfont* font = (const GFXfont*)_custom_font;
        if (font->glyph && '0' >= font->first && '0' <= font->last) {
            return font->glyph['0' - font->first].xAdvance;
        }
        return font->yAdvance / 2;  // Rough estimate
    }
    return 8 * _font_scale;
}

uint8_t RA8875::getFontHeight() {
    if (_custom_font) {
        const GFXfont* font = (const GFXfont*)_custom_font;
        return font->yAdvance;
    }
    return 16 * _font_scale;
}

uint16_t RA8875::Color24To565(uint32_t color24) {
    uint8_t r = (color24 >> 16) & 0xFF;
    uint8_t g = (color24 >> 8) & 0xFF;
    uint8_t b = color24 & 0xFF;

    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;

    return (r5 << 11) | (g6 << 5) | b5;
}

uint16_t RA8875::Color565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;

    return (r5 << 11) | (g6 << 5) | b5;
}

void RA8875::drawPixels(uint16_t* pixels, uint16_t count, uint16_t x, uint16_t y) {
    if (!g_initialized || !pixels) return;

    for (uint16_t i = 0; i < count && (x + i) < DISPLAY_WIDTH; i++) {
        set_pixel(x + i, y, rgb565_to_argb8888(pixels[i]));
    }
}

void RA8875::drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    // Bresenham's line algorithm
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    int x = x0, y = y0;

    while (true) {
        set_pixel(x, y, argb);

        if (x == x1 && y == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void RA8875::drawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++) {
        set_pixel(x, py, argb);
    }
}

void RA8875::drawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    if (!g_initialized) return;

    uint32_t argb = rgb565_to_argb8888(color);

    for (int px = x; px < x + w && px < DISPLAY_WIDTH; px++) {
        set_pixel(px, y, argb);
    }
}

void RA8875::useLayers(bool enable) {
    g_layers_enabled = enable;
}

void RA8875::layerEffect(uint8_t effect) {
    g_layer_effect = effect;
}

void RA8875::writeTo(uint8_t layer) {
    g_current_layer = layer;
}

void RA8875::clearMemory() {
    if (!g_initialized) return;

    uint32_t black = 0xFF000000;

    if (g_layer1) {
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            g_layer1[i] = black;
        }
    }

    if (g_layer2) {
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            g_layer2[i] = black;
        }
    }
}

void RA8875::BTE_move(uint16_t src_x, uint16_t src_y, uint16_t width, uint16_t height,
                      uint16_t dst_x, uint16_t dst_y, uint8_t rop, uint8_t bte_operation) {
    if (!g_initialized) return;

    uint32_t* buffer = get_current_buffer();
    if (!buffer) return;

    // Create temporary buffer for the move
    uint32_t* temp = new uint32_t[width * height];

    // Copy source region to temp
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = (src_y + y) * DISPLAY_WIDTH + (src_x + x);
            if (src_x + x < DISPLAY_WIDTH && src_y + y < DISPLAY_HEIGHT) {
                temp[y * width + x] = buffer[src_idx];
            }
        }
    }

    // Copy temp to destination
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dst_idx = (dst_y + y) * DISPLAY_WIDTH + (dst_x + x);
            if (dst_x + x < DISPLAY_WIDTH && dst_y + y < DISPLAY_HEIGHT) {
                buffer[dst_idx] = temp[y * width + x];
            }
        }
    }

    delete[] temp;
}

bool RA8875::readStatus() {
    // Always return false (operation complete) in simulator
    return false;
}

void RA8875::writeRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data) {
    if (!g_initialized || !data) return;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int screen_x = x + px;
            int screen_y = y + py;
            if (screen_x < DISPLAY_WIDTH && screen_y < DISPLAY_HEIGHT) {
                set_pixel(screen_x, screen_y, rgb565_to_argb8888(data[py * w + px]));
            }
        }
    }
}

// Public function to update the SDL display - call once per frame
void RA8875::updateScreen() {
    update_display();
}

// Cleanup function - call at program exit
void RA8875_SDL_Cleanup() {
    if (g_texture) {
        SDL_DestroyTexture(g_texture);
        g_texture = nullptr;
    }
    if (g_help_texture) {
        SDL_DestroyTexture(g_help_texture);
        g_help_texture = nullptr;
    }
    if (g_register_texture) {
        SDL_DestroyTexture(g_register_texture);
        g_register_texture = nullptr;
    }
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    if (g_framebuffer) {
        delete[] g_framebuffer;
        g_framebuffer = nullptr;
    }
    if (g_help_buffer) {
        delete[] g_help_buffer;
        g_help_buffer = nullptr;
    }
    if (g_register_buffer) {
        delete[] g_register_buffer;
        g_register_buffer = nullptr;
    }
    if (g_layer1) {
        delete[] g_layer1;
        g_layer1 = nullptr;
    }
    if (g_layer2) {
        delete[] g_layer2;
        g_layer2 = nullptr;
    }
    SDL_Quit();
    g_initialized = false;
}

#endif // USE_SDL_DISPLAY
