#ifndef LPFBOARD_H
#define LPFBOARD_H
#include "SDT.h"

/**
 * @brief Initialize the LPF board hardware and all subsystems
 * @return ESUCCESS on success, ENOI2C if I2C communication fails
 * @note Initializes BPF path control, XVTR, PA, antenna selection, and SWR measurement
 */
errno_t InitializeLPFBoard(void);

/**
 * @brief Route TX signal through band-pass filters
 * @note Enables BPF in transmit path for harmonic suppression
 */
void TXSelectBPF(void);

/**
 * @brief Bypass band-pass filters in TX path
 * @note Used for direct transmission without BPF filtering
 */
void TXBypassBPF(void);

/**
 * @brief Route RX signal through band-pass filters
 * @note Enables BPF in receive path for selectivity
 */
void RXSelectBPF(void);

/**
 * @brief Bypass band-pass filters in RX path
 * @note Used for wideband reception without BPF filtering
 */
void RXBypassBPF(void);

/**
 * @brief Initialize the BPF path control hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitBPFPathControl(void);

/**
 * @brief Enable transverter (XVTR) mode
 * @note Routes signals through external transverter for VHF/UHF operation
 */
void SelectXVTR(void);

/**
 * @brief Bypass transverter and use direct HF path
 * @note Returns to normal HF operation without transverter
 */
void BypassXVTR(void);

/**
 * @brief Initialize the transverter control hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitXVTRControl(void);

/**
 * @brief Enable the 100W power amplifier
 * @note Routes TX signal through external 100W PA
 */
void Select100WPA(void);

/**
 * @brief Bypass the 100W power amplifier
 * @note Uses internal low-power transmit path
 */
void Bypass100WPA(void);

/**
 * @brief Initialize the 100W PA control hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t Init100WPAControl(void);

/**
 * @brief Select low-pass filter for specified amateur radio band
 * @param band Band number (0-10): 60M, 160M, 80M, 40M, 30M, 20M, 17M, 15M, 12M, 10M, 6M
 * @note Automatically switches appropriate LPF for harmonic suppression
 */
void SelectLPFBand(int32_t band);

/**
 * @brief Select antenna output connector
 * @param antenna Antenna number (0-3) for multi-antenna switching
 * @note Configures antenna relay matrix for selected output
 */
void SelectAntenna(uint8_t antenna);

/**
 * @brief Initialize the antenna selection control hardware
 * @return ESUCCESS on success, error code on failure
 */
errno_t InitAntennaControl(void);

/**
 * @brief Read Standing Wave Ratio (SWR) from directional coupler
 * @return SWR value as a ratio (e.g., 1.5 for 1.5:1 SWR)
 * @note Calculated from forward and reflected power measurements
 */
float32_t ReadSWR(void);

/**
 * @brief Read forward power from directional coupler
 * @return Forward power in watts
 * @note Measured via AD7991 ADC from directional coupler
 */
float32_t ReadForwardPower(void);

/**
 * @brief Read reflected power from directional coupler
 * @return Reflected power in watts
 * @note Measured via AD7991 ADC from directional coupler
 */
float32_t ReadReflectedPower(void);

/**
 * @brief Initialize the SWR measurement hardware (AD7991 ADC)
 * @return ESUCCESS on success, ENOI2C if I2C communication fails
 */
errno_t InitSWRControl(void);

/**
 * @brief Convert band number to BCD format for filter switching
 * @param band Band number (0-10)
 * @return 4-bit BCD value for band selection
 * @note BCD format required by LPF relay driver circuitry
 */
uint8_t BandToBCD(int32_t band);

/**
 * @brief Update MCP23017 GPIO registers with current state
 * @note Writes cached register values to hardware via I2C
 */
void UpdateMCPRegisters(void);

// For unit testing - access to internal register state

/**
 * @brief Get the current LPF register state from hardwareRegister
 * @return 16-bit register state value
 * @note This function is intended for unit testing only
 */
uint16_t GetLPFRegisterState(void);

/**
 * @brief Set the LPF register state in hardwareRegister
 * @param value 16-bit register state to set
 * @note This function is intended for unit testing only
 */
void SetLPFRegisterState(uint16_t value);

/**
 * @brief Get the current MCP23017 GPIO register values
 * @return 16-bit value combining GPIOA and GPIOB
 * @note This function is intended for unit testing only
 */
uint16_t GetLPFMCPRegisters(void);

/**
 * @brief Get the cached GPIOA register value
 * @return 8-bit GPIOA cache value
 * @note This function is intended for unit testing only
 */
uint8_t GetLPFMCPAOld(void);

/**
 * @brief Get the cached GPIOB register value
 * @return 8-bit GPIOB cache value
 * @note This function is intended for unit testing only
 */
uint8_t GetLPFMCPBOld(void);

/**
 * @brief Set the cached GPIOA register value
 * @param value 8-bit value to store in GPIOA cache
 * @note This function is intended for unit testing only
 */
void SetLPFMCPAOld(uint8_t value);

/**
 * @brief Set the cached GPIOB register value
 * @param value 8-bit value to store in GPIOB cache
 * @note This function is intended for unit testing only
 */
void SetLPFMCPBOld(uint8_t value);

#endif // LPFBOARD_H
