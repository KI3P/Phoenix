#ifndef FRONTPANEL_USBHOST_H
#define FRONTPANEL_USBHOST_H
#include "SDT.h"

/*
 * USB Host driver -- owns the USBHost / USBHub / parser singletons used by
 * the keyboard and mouse modules. Ported from T41_SDR/t41USBHost.cpp
 * (plumbing only; first-pass port).
 *
 * Gated by USB_HOST_STACK_ENABLED in Config.h, which is automatically defined
 * when either USB_HOST_INPUT_ENABLED (HID keyboard/mouse) or
 * DMR_THUMBDV_ENABLED (DVSI ThumbDV / AMBE3000 USB serial) is set. When
 * USB_HOST_STACK_ENABLED is not defined (default), this module compiles to
 * no-ops and pulls no extra dependencies.
 *
 * Multiple subsystems (keyboard/mouse HID, ThumbDV CDC) share a single
 * usbHost singleton: InitializeUSBHost() calls usbHost.begin() exactly once
 * and TickUSBHost() calls usbHost.Task() exactly once per main-loop pass.
 *
 * Phoenix integration:
 *   InitializeUSBHost() should be called once from setup() (Globals.cpp).
 *   TickUSBHost() should be called each iteration of the main loop
 *   (Loop.cpp). It runs the host stack and pumps the mouse driver and the
 *   ThumbDV driver.
 *
 * The keyboard buffer is drained by callers via FrontPanel_USBKeyboard.h
 * (KeyboardReadChar()).
 */

/**
 * @brief Initialize the USB host stack and any attached HID devices.
 * @note No-op when USB_HOST_STACK_ENABLED is not defined in Config.h.
 *       When enabled, calls usbHost.begin(); when USB_HOST_INPUT_ENABLED is
 *       also defined, calls KeyboardSetup() and MouseSetup() too.
 */
void InitializeUSBHost(void);

/**
 * @brief Pump the USB host stack and any per-subsystem polling loops.
 * @note Call once per main-loop iteration. No-op when USB_HOST_STACK_ENABLED
 *       is not defined. When enabled, calls usbHost.Task() and (when the
 *       respective subsystem flag is set) MouseLoop() and TickAmbeDvsi().
 */
void TickUSBHost(void);

#endif /* FRONTPANEL_USBHOST_H */
