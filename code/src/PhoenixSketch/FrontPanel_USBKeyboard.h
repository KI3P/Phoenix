#ifndef FRONTPANEL_USBKEYBOARD_H
#define FRONTPANEL_USBKEYBOARD_H
#include "SDT.h"

/*
 * USB Keyboard driver -- ported from T41_SDR/keyboard.cpp + keyboard.h.
 * Plumbing only; gated by USB_HOST_INPUT_ENABLED in Config.h.
 *
 * Key API differences from T41:
 *   - T41's bare getc() / putc() are renamed to KeyboardReadChar() /
 *     KeyboardWriteChar() to avoid clashing with stdio.h's getc/putc
 *     (Phoenix's SDT.h includes <stdio.h>).
 *   - putc-equivalent is internal (the USB callbacks fill the buffer);
 *     KeyboardWriteChar() is exposed only for unit-test injection.
 *
 * Caller pattern (e.g. for the future CW message keyer's input loop):
 *
 *     uint8_t c;
 *     while ((c = KeyboardReadChar()) != 0) {
 *         // dispatch c
 *     }
 *
 * A return value of 0 means "no character available" (matches T41).
 */

#define KEYBOARD_BUFFER_SIZE 256

extern uint8_t kbBuffer[KEYBOARD_BUFFER_SIZE];
extern uint8_t kbIndexIn, kbIndexOut;

/**
 * @brief Initialize the USB HID keyboard callbacks and clear the ring buffer.
 * @note Called by InitializeUSBHost(). No-op when USB_HOST_INPUT_ENABLED is
 *       not defined.
 */
void KeyboardSetup(void);

/**
 * @brief Read the next ASCII character from the keyboard ring buffer.
 * @return The next character, or 0 if the buffer is empty.
 * @note Renamed from T41's getc() to avoid stdio.h name clash. Returns
 *       0 (and is a no-op) when USB_HOST_INPUT_ENABLED is not defined.
 */
uint8_t KeyboardReadChar(void);

/**
 * @brief Inject a character into the keyboard ring buffer.
 * @param c Character to inject.
 * @note Internal use / unit-test hook. Production code should not call this;
 *       characters arrive via the USB HID release callback. No-op when
 *       USB_HOST_INPUT_ENABLED is not defined.
 */
void KeyboardWriteChar(uint8_t c);

#endif /* FRONTPANEL_USBKEYBOARD_H */
