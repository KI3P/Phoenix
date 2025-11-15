#ifndef RFBOARD_H
#define RFBOARD_H
#include "SDT.h"

// Attenuator control functions

/**
 * @brief Initialize the TX attenuator hardware and set initial attenuation value
 * @param txAttenuation_dB Initial transmit attenuation in decibels (0.0 to 31.5 dB)
 * @return ESUCCESS on success, ENOI2C if I2C communication fails
 */
errno_t TXAttenuatorCreate(float32_t txAttenuation_dB);

/**
 * @brief Initialize the RX attenuator hardware and set initial attenuation value
 * @param rxAttenuation_dB Initial receive attenuation in decibels (0.0 to 31.5 dB)
 * @return ESUCCESS on success, ENOI2C if I2C communication fails
 */
errno_t RXAttenuatorCreate(float32_t rxAttenuation_dB);

/**
 * @brief Set the receive path attenuation value
 * @param rxAttenuation_dB Desired RX attenuation in decibels (0.0 to 31.5 dB in 0.5 dB steps)
 * @return ESUCCESS on success, error code on failure
 * @note Values are automatically clamped to valid range and rounded to nearest 0.5 dB
 */
errno_t SetRXAttenuation(float32_t rxAttenuation_dB);

/**
 * @brief Set the transmit path attenuation value
 * @param txAttenuation_dB Desired TX attenuation in decibels (0.0 to 31.5 dB in 0.5 dB steps)
 * @return ESUCCESS on success, error code on failure
 * @note Values are automatically clamped to valid range and rounded to nearest 0.5 dB
 */
errno_t SetTXAttenuation(float32_t txAttenuation_dB);

/**
 * @brief Get the current receive path attenuation setting
 * @return Current RX attenuation in decibels
 */
float32_t GetRXAttenuation(void);

/**
 * @brief Get the current transmit path attenuation setting
 * @return Current TX attenuation in decibels
 */
float32_t GetTXAttenuation(void);

/**
 * @brief Initialize both TX and RX attenuator hardware subsystems
 * @return ESUCCESS on success, error code on I2C failure
 */
errno_t InitAttenuation(void);

/**
 * @brief Get the current MCP23017 GPIO register state for testing
 * @return 16-bit register value combining GPIOA and GPIOB
 * @note This function is intended for unit testing only
 */
uint16_t GetRFMCPRegisters(void);

// SSB VFO Control Functions

void SetFrequencyCorrectionFactor(int32_t corr);

/**
 * @brief Get the current SSB VFO frequency setting
 * @return Frequency in decihertz (Hz × 10)
 */
int64_t GetSSBVFOFrequency(void);

/**
 * @brief Set the SSB VFO frequency
 * @param frequency_dHz Desired frequency in decihertz (Hz × 10)
 * @note Frequency is applied to Si5351 CLK0 output
 */
void SetSSBVFOFrequency(int64_t frequency_dHz);

/**
 * @brief Set the SSB VFO output power level
 * @param power Power level (0-3): SI5351_DRIVE_2MA, 4MA, 6MA, or 8MA
 */
void SetSSBVFOPower(int32_t power);

/**
 * @brief Enable the SSB VFO output
 * @note Turns on Si5351 CLK0 output
 */
void EnableSSBVFOOutput(void);

/**
 * @brief Disable the SSB VFO output
 * @note Turns off Si5351 CLK0 output
 */
void DisableSSBVFOOutput(void);

/**
 * @brief Initialize the SSB VFO hardware (Si5351 CLK0)
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitSSBVFO(void);

// CW VFO Control Functions

/**
 * @brief Get the current CW VFO frequency setting
 * @return Frequency in decihertz (Hz × 10)
 */
int64_t GetCWVFOFrequency(void);

/**
 * @brief Set the CW VFO frequency
 * @param frequency_dHz Desired frequency in decihertz (Hz × 10)
 * @note Frequency is applied to Si5351 CLK1 output
 */
void SetCWVFOFrequency(int64_t frequency_dHz);

/**
 * @brief Set the CW VFO output power level
 * @param power Power level (0-3): SI5351_DRIVE_2MA, 4MA, 6MA, or 8MA
 */
void SetCWVFOPower(int32_t power);

/**
 * @brief Enable the CW VFO output
 * @note Turns on Si5351 CLK1 output
 */
void EnableCWVFOOutput(void);

/**
 * @brief Disable the CW VFO output
 * @note Turns off Si5351 CLK1 output
 */
void DisableCWVFOOutput(void);

/**
 * @brief Initialize the CW VFO hardware (Si5351 CLK1)
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitCWVFO(void);

/**
 * @brief Turn on CW carrier (key down)
 * @note Enables CW VFO output for Morse transmission
 */
void CWon(void);

/**
 * @brief Turn off CW carrier (key up)
 * @note Disables CW VFO output between Morse elements
 */
void CWoff(void);

/**
 * @brief Initialize both SSB and CW VFO subsystems
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitVFOs(void);

// Transmit Modulation Control

/**
 * @brief Select SSB modulation mode for transmit
 * @note Configures RF routing for SSB transmission path
 */
void SelectTXSSBModulation(void);

/**
 * @brief Select CW modulation mode for transmit
 * @note Configures RF routing for CW transmission path
 */
void SelectTXCWModulation(void);

/**
 * @brief Initialize the transmit modulation selection hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitTXModulation(void);

// Calibration Control

/**
 * @brief Enable calibration feedback path
 * @note Connects TX output back to RX input for IQ calibration
 */
void EnableCalFeedback(void);

/**
 * @brief Disable calibration feedback path
 * @note Restores normal TX/RX signal routing
 */
void DisableCalFeedback(void);

/**
 * @brief Initialize the calibration feedback control hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitCalFeedbackControl(void);

// RXTX Control

/**
 * @brief Switch hardware to transmit mode
 * @note Activates transmit relays and disables receive path
 */
void SelectTXMode(void);

/**
 * @brief Switch hardware to receive mode
 * @note Activates receive path and disables transmit relays
 */
void SelectRXMode(void);

/**
 * @brief Initialize the RX/TX switching hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitRXTX(void);

// Function calls needed for unit tests

/**
 * @brief Get the current RX/TX state for testing
 * @return true if in TX mode, false if in RX mode
 * @note This function is intended for unit testing only
 */
bool getRXTXState(void);

/**
 * @brief Get the current CW carrier state for testing
 * @return true if CW carrier is on (key down), false if off (key up)
 * @note This function is intended for unit testing only
 */
bool getCWState(void);

/**
 * @brief Get the current calibration feedback state for testing
 * @return true if calibration feedback is enabled, false if disabled
 * @note This function is intended for unit testing only
 */
bool getCalFeedbackState(void);

/**
 * @brief Get the current modulation mode for testing
 * @return true if CW mode selected, false if SSB mode selected
 * @note This function is intended for unit testing only
 */
bool getModulationState(void);

#endif // RFBOARD_H
