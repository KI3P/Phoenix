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
#ifndef MODE_H
#define MODE_H

/**
 * @brief Update the hardware and DSP state
 * @note Called by ModeSm state machine when entering new modes
 */
void UpdateHardwareState(void);

// CW Transmit Space Mode (key up)

/**
 * @brief Enter CW transmit space state (carrier off)
 * @note Called by ModeSm state machine when entering CW_TRANSMIT_SPACE state
 * @note Updates RF hardware state and audio I/O for CW key-up condition
 */
void ModeCWTransmitSpaceEnter(void);

/**
 * @brief Checks if transmit is allowed for the current band 
 * @note Used as guard for transitions to transmit states
 */
bool IsTxAllowed(void);

#endif // MODE_H
