/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

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
