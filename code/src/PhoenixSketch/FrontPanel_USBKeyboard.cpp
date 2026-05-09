#include "SDT.h"
#include "FrontPanel_USBKeyboard.h"

/*
 * USB keyboard driver -- ported from T41_SDR/keyboard.cpp.
 * Gated by USB_HOST_INPUT_ENABLED in Config.h.
 */

uint8_t kbBuffer[KEYBOARD_BUFFER_SIZE];
uint8_t kbIndexIn  = 0;
uint8_t kbIndexOut = 0;

#ifdef USB_HOST_INPUT_ENABLED

#include <USBHost_t36.h>  // vendored at code/src/USBHost_t36/; see that dir's VENDORED.md

extern KeyboardController kbController;  // defined in FrontPanel_USBHost.cpp

// Tracks the most recent raw HID press so that OnRelease can recover the
// keycode for keys that the PJRC library mis-reports as 0 when caps-lock is
// on (specifically space, scancode 44, and backspace, scancode 42). Bug
// noted in T41's keyboard.cpp comments and worked around the same way here.
static uint8_t s_rawPressed = 0;

static void OnRawPress(uint8_t unicode) {
    s_rawPressed = unicode;
}

static void OnRelease(int unicode) {
    // PJRC library workaround: caps-lock + space/backspace come through with
    // unicode == 0. Recover from the last raw press.
    if (unicode == 0) {
        switch (s_rawPressed) {
            case 42:  // backspace scancode -- inject 127 (DEL)
                KeyboardWriteChar(127);
                break;
            case 44:  // space scancode -- inject 32 (' ')
                KeyboardWriteChar(32);
                break;
            default:
                return;
        }
        return;
    }
    KeyboardWriteChar((uint8_t)(unicode & 0xff));
}

void KeyboardSetup(void) {
    kbController.attachRelease(OnRelease);
    kbController.attachRawPress(OnRawPress);
    kbIndexIn  = 0;
    kbIndexOut = 0;
    kbBuffer[0] = 0;
}

uint8_t KeyboardReadChar(void) {
    if (kbIndexIn == kbIndexOut) {
        return 0;
    }
    uint8_t c = kbBuffer[kbIndexOut++];
    // 256-byte buffer indexed by uint8_t -- wraps naturally on overflow,
    // but be explicit in case the buffer size is ever changed.
    if (kbIndexOut >= KEYBOARD_BUFFER_SIZE) kbIndexOut = 0;
    return c;
}

void KeyboardWriteChar(uint8_t c) {
    kbBuffer[kbIndexIn++] = c;
    if (kbIndexIn >= KEYBOARD_BUFFER_SIZE) kbIndexIn = 0;
}

#else  /* USB_HOST_INPUT_ENABLED */

void KeyboardSetup(void) {
    kbIndexIn  = 0;
    kbIndexOut = 0;
}

uint8_t KeyboardReadChar(void) {
    return 0;
}

void KeyboardWriteChar(uint8_t c) {
    (void)c;
}

#endif  /* USB_HOST_INPUT_ENABLED */
