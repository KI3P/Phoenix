#ifndef FRONTPANEL_H
#define FRONTPANEL_H
#include "SDT.h"

/**
 * @brief Initialize the front panel hardware (buttons, LEDs, rotary encoders)
 * @note Configures GPIO interrupts for button press detection and initializes LED control
 */
void InitializeFrontPanel(void);

/**
 * @brief Check for and process front panel button/encoder interrupts
 * @note Called from main loop to handle user input events
 * @note Debounces inputs and queues button events for state machine processing
 */
void CheckForFrontPanelInterrupts(void);

/**
 * @brief Get the most recent button press event from the queue
 * @return Button ID of pressed button, or -1 if no button pressed
 * @note Button IDs defined in SDT.h (BUTTON_*)
 */
int32_t GetButton(void);

/**
 * @brief Set button state for testing purposes
 * @param button Button ID to simulate press
 * @note This function is primarily used for unit testing button event handling
 */
void SetButton(int32_t button);

/**
 * @brief Set the state of a front panel LED
 * @param led LED number (0-7) to control
 * @param state LED state: 0=OFF, 1=ON
 * @note LEDs are controlled via GPIO expander on front panel
 */
void FrontPanelSetLed(uint8_t led, uint8_t state);

#endif // FRONTPANEL_H
