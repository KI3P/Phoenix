#ifndef MODE_H
#define MODE_H

// SSB Receive Mode

/**
 * @brief Enter SSB receive mode
 * @note Called by ModeSm state machine when entering SSB_RECEIVE state
 * @note Updates RF hardware state and audio I/O configuration for SSB reception
 */
void ModeSSBReceiveEnter(void);

/**
 * @brief Exit SSB receive mode
 * @note Called by ModeSm state machine when leaving SSB_RECEIVE state
 * @note Currently performs no cleanup actions
 */
void ModeSSBReceiveExit(void);

// SSB Transmit Mode

/**
 * @brief Enter SSB transmit mode
 * @note Called by ModeSm state machine when entering SSB_TRANSMIT state
 * @note Updates RF hardware state and audio I/O configuration for SSB transmission
 */
void ModeSSBTransmitEnter(void);

/**
 * @brief Exit SSB transmit mode
 * @note Called by ModeSm state machine when leaving SSB_TRANSMIT state
 * @note Currently performs no cleanup actions
 */
void ModeSSBTransmitExit(void);

// CW Receive Mode

/**
 * @brief Enter CW receive mode
 * @note Called by ModeSm state machine when entering CW_RECEIVE state
 * @note Updates RF hardware state and audio I/O configuration for CW reception
 */
void ModeCWReceiveEnter(void);

/**
 * @brief Exit CW receive mode
 * @note Called by ModeSm state machine when leaving CW_RECEIVE state
 * @note Currently performs no cleanup actions
 */
void ModeCWReceiveExit(void);

// CW Transmit Mark Mode (key down)

/**
 * @brief Enter CW transmit mark state (carrier on)
 * @note Called by ModeSm state machine when entering CW_TRANSMIT_MARK state
 * @note Updates RF hardware state and audio I/O for CW transmission with key down
 */
void ModeCWTransmitMarkEnter(void);

/**
 * @brief Exit CW transmit mark state
 * @note Called by ModeSm state machine when leaving CW_TRANSMIT_MARK state
 * @note Currently performs no cleanup actions
 */
void ModeCWTransmitMarkExit(void);

// CW Transmit Space Mode (key up)

/**
 * @brief Enter CW transmit space state (carrier off)
 * @note Called by ModeSm state machine when entering CW_TRANSMIT_SPACE state
 * @note Updates RF hardware state and audio I/O for CW key-up condition
 */
void ModeCWTransmitSpaceEnter(void);

/**
 * @brief Exit CW transmit space state
 * @note Called by ModeSm state machine when leaving CW_TRANSMIT_SPACE state
 * @note Updates hardware to prepare for next keying transition
 */
void ModeCWTransmitSpaceExit(void);

// Frequency Calibration Mode

/**
 * @brief Enter frequency calibration mode
 * @note Called by ModeSm state machine when entering CALIBRATE_FREQUENCY state
 * @note Displays frequency calibration screen and enables calibration controls
 */
void CalibrateFrequencyEnter(void);

/**
 * @brief Exit frequency calibration mode
 * @note Called by ModeSm state machine when leaving CALIBRATE_FREQUENCY state
 * @note Saves calibration data and returns to normal operation
 */
void CalibrateFrequencyExit(void);

// TX I/Q Calibration Mode

/**
 * @brief Enter TX I/Q calibration mode
 * @note Called by ModeSm state machine when entering CALIBRATE_TXIQ state
 * @note Displays TX IQ calibration screen and enables feedback path
 */
void CalibrateTXIQEnter(void);

/**
 * @brief Exit TX I/Q calibration mode
 * @note Called by ModeSm state machine when leaving CALIBRATE_TXIQ state
 * @note Saves TX IQ correction factors and disables feedback path
 */
void CalibrateTXIQExit(void);

// RX I/Q Calibration Mode

/**
 * @brief Enter RX I/Q calibration mode
 * @note Called by ModeSm state machine when entering CALIBRATE_RXIQ state
 * @note Displays RX IQ calibration screen and prepares test signal
 */
void CalibrateRXIQEnter(void);

/**
 * @brief Exit RX I/Q calibration mode
 * @note Called by ModeSm state machine when leaving CALIBRATE_RXIQ state
 * @note Saves RX IQ correction factors and returns to normal operation
 */
void CalibrateRXIQExit(void);

// CW PA Calibration Mode

/**
 * @brief Enter CW power amplifier calibration mode
 * @note Called by ModeSm state machine when entering CALIBRATE_CWPA state
 * @note Displays CW PA calibration screen and enables test transmission
 */
void CalibrateCWPAEnter(void);

/**
 * @brief Exit CW PA calibration mode
 * @note Called by ModeSm state machine when leaving CALIBRATE_CWPA state
 * @note Saves CW PA settings and returns to normal operation
 */
void CalibrateCWPAExit(void);

// SSB PA Calibration Mode

/**
 * @brief Enter SSB power amplifier calibration mode
 * @note Called by ModeSm state machine when entering CALIBRATE_SSBPA state
 * @note Displays SSB PA calibration screen and enables test transmission
 */
void CalibrateSSBPAEnter(void);

/**
 * @brief Exit SSB PA calibration mode
 * @note Called by ModeSm state machine when leaving CALIBRATE_SSBPA state
 * @note Saves SSB PA settings and returns to normal operation
 */
void CalibrateSSBPAExit(void);

// Calibration Trigger Functions

/**
 * @brief Trigger transition to frequency calibration mode
 * @note Dispatches CALIBRATE_FREQUENCY event to ModeSm state machine
 * @note Called from menu system when user selects frequency calibration
 */
void TriggerCalibrateFrequency(void);

/**
 * @brief Trigger exit from calibration mode
 * @note Dispatches EXIT_CALIBRATION event to ModeSm state machine
 * @note Called when user completes or cancels calibration
 */
void TriggerCalibrateExit(void);

/**
 * @brief Trigger transition to RX I/Q calibration mode
 * @note Dispatches CALIBRATE_RXIQ event to ModeSm state machine
 * @note Called from menu system when user selects RX IQ calibration
 */
void TriggerCalibrateRXIQ(void);

/**
 * @brief Trigger transition to TX I/Q calibration mode
 * @note Dispatches CALIBRATE_TXIQ event to ModeSm state machine
 * @note Called from menu system when user selects TX IQ calibration
 */
void TriggerCalibrateTXIQ(void);

/**
 * @brief Trigger transition to CW PA calibration mode
 * @note Dispatches CALIBRATE_CWPA event to ModeSm state machine
 * @note Called from menu system when user selects CW PA calibration
 */
void TriggerCalibrateCWPA(void);

/**
 * @brief Trigger transition to SSB PA calibration mode
 * @note Dispatches CALIBRATE_SSBPA event to ModeSm state machine
 * @note Called from menu system when user selects SSB PA calibration
 */
void TriggerCalibrateSSBPA(void);


#endif // MODE_H
