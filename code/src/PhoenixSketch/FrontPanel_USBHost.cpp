#include "SDT.h"
#include "FrontPanel_USBHost.h"
#include "FrontPanel_USBKeyboard.h"
#include "FrontPanel_USBMouse.h"
#ifdef DMR_THUMBDV_ENABLED
#include "AmbeDvsi.h"
#endif

/*
 * USB host driver -- ported from T41_SDR/t41USBHost.cpp. Plumbing only:
 * everything is gated by USB_HOST_STACK_ENABLED (defined automatically in
 * Config.h when either USB_HOST_INPUT_ENABLED or DMR_THUMBDV_ENABLED is
 * set), so the default Phoenix build has no library dependency.
 */

#ifdef USB_HOST_STACK_ENABLED

#include <USBHost_t36.h>  // vendored at code/src/USBHost_t36/ (symlinked or via Teensyduino); see VENDORED.md

// Singletons. Owned by this translation unit; referenced by the keyboard,
// mouse, and ThumbDV drivers via 'extern' in their .cpp files.
USBHost           usbHost;
USBHub            usbHub(usbHost);

#ifdef USB_HOST_INPUT_ENABLED
USBHIDParser      hkbParser(usbHost);    // each HID device needs its own parser
KeyboardController kbController(usbHost);
USBHIDParser      mouseParser(usbHost);
MouseController   mouseController(usbHost);
#endif

void InitializeUSBHost(void) {
    usbHost.begin();
#ifdef USB_HOST_INPUT_ENABLED
    KeyboardSetup();
    MouseSetup();
#endif
#ifdef DMR_THUMBDV_ENABLED
    InitializeAmbeDvsi();
#endif
    delay(1000);  // matches T41 -- give attached devices a moment to enumerate
}

void TickUSBHost(void) {
    usbHost.Task();
#ifdef USB_HOST_INPUT_ENABLED
    MouseLoop();
#endif
#ifdef DMR_THUMBDV_ENABLED
    TickAmbeDvsi();
#endif
}

#else  /* USB_HOST_STACK_ENABLED */

void InitializeUSBHost(void) {
    // USB host stack disabled. Define USB_HOST_INPUT_ENABLED (HID
    // keyboard/mouse) or DMR_THUMBDV_ENABLED (DVSI ThumbDV codec) in
    // Config.h to enable.
}

void TickUSBHost(void) {
    // USB host stack disabled.
}

#endif  /* USB_HOST_STACK_ENABLED */
