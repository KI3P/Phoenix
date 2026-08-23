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

/**
 * @brief Checks if the radio is transmitting through the SSB transmit path
 * @return true in SSB_TRANSMIT or DIGITAL_TRANSMIT
 * @note Digital mode transmits an SSB signal with the audio sourced from USB, so
 *       the two states behave identically for the display and the DSP chain
 */
bool IsSSBTransmit(void);

// Digital (USB audio) mode

/**
 * @brief Enter digital mode: force 176.4 ksps and start the USB audio transport
 * @note Called by ModeSm state machine when entering the DIGITAL_STATES composite
 * @note Saves the previous sample rate for ExitDigitalMode() to restore
 * @note 176.4 ksps is required: it puts the receive chain's Fs/4 tap at exactly
 *       44,100 Hz, so the USB audio endpoint needs no resampling
 */
void EnterDigitalMode(void);

/**
 * @brief Leave digital mode: stop USB audio and restore the previous sample rate
 * @note Called by ModeSm state machine when exiting the DIGITAL_STATES composite,
 *       including via a calibration event dispatched at the NORMAL_STATES level
 */
void ExitDigitalMode(void);

#endif // MODE_H
