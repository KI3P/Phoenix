#ifndef FRONTPANEL_USBMOUSE_H
#define FRONTPANEL_USBMOUSE_H
#include "SDT.h"

/*
 * USB Mouse driver -- ported from T41_SDR/mouse.cpp + mouse.h.
 *
 * IMPORTANT: only the *driver layer* is ported. T41's MouseLoop() also
 * dispatches clicks/wheel events into T41-specific UI logic (FT8 windows,
 * menu bar, frequency field, spectrum/waterfall pane, info-box, etc.) that
 * has no Phoenix counterpart and would require ~15 stubs for symbols
 * Phoenix doesn't define. Phoenix's UI will own that mapping when somebody
 * decides what mouse-on-spectrum-pane should *do* in Phoenix's pane layout
 * (MainBoard_DisplayHome.cpp etc.).
 *
 * What this driver gives you:
 *   - Owns the polling of mouseController in MouseLoop() (called from
 *     TickUSBHost()).
 *   - Buffers the latest delta-x / delta-y / buttons / wheel into a
 *     MouseEvent struct.
 *   - Exposes MousePollEvent() to consumers; consumer drains the event
 *     and gets {dx, dy, buttons, wheel}.
 *
 * Gated by USB_HOST_INPUT_ENABLED in Config.h.
 */

/**
 * @brief One mouse event (deltas + button mask + wheel delta).
 * @note Buttons follow the USB HID mouse button bitmask: bit0=left,
 *       bit1=right, bit2=middle. Multiple buttons may be set simultaneously.
 *       Matches T41/PJRC convention.
 */
typedef struct {
    int16_t dx;        /**< Relative X movement since last poll */
    int16_t dy;        /**< Relative Y movement since last poll */
    uint8_t buttons;   /**< Bitmask: bit0=left, bit1=right, bit2=middle */
    int8_t  wheel;     /**< Wheel delta (positive=up) */
} MouseEvent;

/**
 * @brief Initialize the USB HID mouse driver.
 * @note Called by InitializeUSBHost(). No-op when USB_HOST_INPUT_ENABLED is
 *       not defined.
 */
void MouseSetup(void);

/**
 * @brief Poll the USB mouse and capture any pending event.
 * @note Called by TickUSBHost() each main-loop iteration. When an event is
 *       available it is stored internally for retrieval via MousePollEvent().
 *       No-op when USB_HOST_INPUT_ENABLED is not defined.
 */
void MouseLoop(void);

/**
 * @brief Drain the most recent mouse event, if any.
 * @param[out] out  Filled with the event when one is available.
 * @return true if an event was drained, false if no event pending.
 * @note Returns false when USB_HOST_INPUT_ENABLED is not defined.
 */
bool MousePollEvent(MouseEvent *out);

#endif /* FRONTPANEL_USBMOUSE_H */
