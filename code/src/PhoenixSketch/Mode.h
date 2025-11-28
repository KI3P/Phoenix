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

#endif // MODE_H
