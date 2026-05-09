#include "SDT.h"
#include "FrontPanel_USBMouse.h"

/*
 * USB mouse driver layer -- ported from T41_SDR/mouse.cpp.
 * Driver-only port: action dispatch (clicks/wheel into UI panes) is left
 * to Phoenix's UI layer when somebody designs that mapping.
 *
 * Gated by USB_HOST_INPUT_ENABLED in Config.h.
 */

#ifdef USB_HOST_INPUT_ENABLED

#include <USBHost_t36.h>  // vendored at code/src/USBHost_t36/; see that dir's VENDORED.md

extern MouseController mouseController;  // defined in FrontPanel_USBHost.cpp

static MouseEvent s_lastEvent  = {0, 0, 0, 0};
static bool       s_eventReady = false;

void MouseSetup(void) {
    s_eventReady = false;
}

void MouseLoop(void) {
    if (!mouseController.available()) {
        return;
    }
    s_lastEvent.dx      = (int16_t)mouseController.getMouseX();
    s_lastEvent.dy      = (int16_t)mouseController.getMouseY();
    s_lastEvent.buttons = (uint8_t)mouseController.getButtons();
    s_lastEvent.wheel   = (int8_t)mouseController.getWheel();
    s_eventReady = true;
    mouseController.mouseDataClear();
}

bool MousePollEvent(MouseEvent *out) {
    if (!s_eventReady || out == nullptr) {
        return false;
    }
    *out = s_lastEvent;
    s_eventReady = false;
    return true;
}

#else  /* USB_HOST_INPUT_ENABLED */

void MouseSetup(void) {
    // USB host input disabled.
}

void MouseLoop(void) {
    // USB host input disabled.
}

bool MousePollEvent(MouseEvent *out) {
    (void)out;
    return false;
}

#endif  /* USB_HOST_INPUT_ENABLED */
