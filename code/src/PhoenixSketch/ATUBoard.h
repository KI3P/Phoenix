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

#ifndef ATUBOARD_H
#define ATUBOARD_H
#include "SDT.h"

// Compiled only when ATU_PRESENT is defined in Config.h (see ATUBoard.cpp).
// The declarations are always visible, so an unguarded call fails at link time.

#define ATU_NUM_INDUCTORS  8
#define ATU_NUM_CAPACITORS 8
#define ATU_NUM_RELAYS     20   // K1 to K20. K19 and K20 share one control line.

/**
 * @brief Initialize the ATU control board (MCP23017 + MCP23008 on Wire2)
 * @return ESUCCESS on success, ENOI2C if either I/O expander does not respond
 * @note Puts the tuner in its safe state: network bypassed, no L, no C,
 *       caps at output side, unbalanced output. Only latches as initialized
 *       on success, so it can be called again to retry.
 */
errno_t InitializeATUBoard(void);

/**
 * @brief Return the ATU to its safe state (every relay de-energised)
 * @note Bypassed, no L, no C, caps at output side, unbalanced output
 */
void ResetATUBoard(void);

/**
 * @brief Select the series inductors
 * @param mask Bit n set puts inductor L(n+1) in series (K(n+1) energised).
 *        L1..L8 = 0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8 uH
 */
void SetATUInductors(uint8_t mask);

/**
 * @brief Get the series inductor selection mask
 * @return Bit n set means inductor L(n+1) is in series
 */
uint8_t GetATUInductors(void);

/**
 * @brief Select the shunt capacitors
 * @param mask Bit n set connects capacitor C(n+1) to ground (K(n+9) energised).
 *        C1..C8 = 10, 22, 47, 100, 220, 470, 1000, 2000 pF
 */
void SetATUCapacitors(uint8_t mask);

/**
 * @brief Get the shunt capacitor selection mask
 * @return Bit n set means capacitor C(n+1) is connected
 */
uint8_t GetATUCapacitors(void);

/**
 * @brief Set inductors and capacitors together, with a single hardware update
 * @param lMask Inductor mask, as for SetATUInductors
 * @param cMask Capacitor mask, as for SetATUCapacitors
 */
void SetATULC(uint8_t lMask, uint8_t cMask);

/**
 * @brief Set every ATU relay at once, with a single hardware update
 * @param networkEngaged true puts the L/C network in line (K19+K20)
 * @param balancedOutput true routes the output to the 4:1 balun (K18)
 * @param capsAtInput true puts the capacitor bank at the input side (K17)
 * @param lMask Inductor mask, as for SetATUInductors
 * @param cMask Capacitor mask, as for SetATUCapacitors
 */
void SetATUState(bool networkEngaged, bool balancedOutput, bool capsAtInput,
                 uint8_t lMask, uint8_t cMask);

/**
 * @brief Return true if it is safe to switch the ATU relays now
 * @return true only in the receive state; relays must not switch with RF present
 * @note The setters do not check this themselves; callers driven by the user
 *       (menu, CAT) must check it first
 */
bool ATUChangeAllowed(void);

/**
 * @brief Put the capacitor bank on the input (transmitter) side of the inductors (C-L)
 * @note Energises K17
 */
void ATUSelectCapsAtInput(void);

/**
 * @brief Put the capacitor bank on the output (antenna) side of the inductors (L-C)
 * @note De-energises K17
 */
void ATUSelectCapsAtOutput(void);

/**
 * @brief Return true if the capacitor bank is on the input side (C-L)
 */
bool ATUCapsAtInput(void);

/**
 * @brief Route the tuner output to the 4:1 balun (balanced output)
 * @note Energises K18
 */
void ATUSelectBalancedOutput(void);

/**
 * @brief Route the tuner output to the RF OUTPUT coax connector
 * @note De-energises K18
 */
void ATUSelectUnbalancedOutput(void);

/**
 * @brief Return true if the balanced (balun) output is selected
 */
bool ATUBalancedOutput(void);

/**
 * @brief Put the L/C network in line between input and output
 * @note Energises K19 and K20 (the line the schematic calls "BYPASS")
 */
void ATUEngageNetwork(void);

/**
 * @brief Bypass the L/C network: input connected straight to output
 * @note De-energises K19 and K20. This is also the power-off state.
 */
void ATUBypassNetwork(void);

/**
 * @brief Return true if the L/C network is in line (not bypassed)
 */
bool ATUNetworkEngaged(void);

/**
 * @brief Energise or de-energise a relay by its schematic number
 * @param k Relay number, 1 to 20 (K19 and K20 share one control line)
 * @param energise true to energise the coil
 * @return ESUCCESS, or EFAIL if k is out of range
 */
errno_t SetATURelay(uint8_t k, bool energise);

/**
 * @brief Return whether a relay is energised, by its schematic number
 * @param k Relay number, 1 to 20
 * @return true if energised; false if not, or if k is out of range
 */
bool GetATURelay(uint8_t k);

/**
 * @brief Nominal value of one inductor
 * @param n Inductor number, 1 to 8
 * @return Inductance in uH, or 0 if n is out of range
 */
float32_t ATUInductorValue_uH(uint8_t n);

/**
 * @brief Nominal value of one capacitor
 * @param n Capacitor number, 1 to 8
 * @return Capacitance in pF, or 0 if n is out of range
 */
float32_t ATUCapacitorValue_pF(uint8_t n);

/**
 * @brief Total nominal series inductance currently selected
 * @return Inductance in uH
 */
float32_t GetATUInductance_uH(void);

/**
 * @brief Total nominal shunt capacitance currently selected
 * @return Capacitance in pF
 */
float32_t GetATUCapacitance_pF(void);

/**
 * @brief Write the ATU state in hardwareRegister to the I/O expanders
 * @note Only writes the ports whose value has changed since the last write
 */
void UpdateATURegisters(void);

// For unit testing - access to internal register state

/**
 * @brief Get the ATU part of hardwareRegister (bits 33-51)
 * @return 19-bit ATU state, bit 0 = L1 ... bit 18 = network engaged
 * @note This function is intended for unit testing only
 */
uint32_t GetATURegisterState(void);

/**
 * @brief Set the ATU part of hardwareRegister (bits 33-51) without touching hardware
 * @param value 19-bit ATU state, bit 0 = L1 ... bit 18 = network engaged
 * @note This function is intended for unit testing only
 */
void SetATURegisterState(uint32_t value);

/**
 * @brief Read the I/O expander outputs back over I2C
 * @return Bits 0-7 MCP23017 GPA (L), bits 8-15 GPB (C), bits 16-23 MCP23008 GP
 * @note This function is intended for testing only
 */
uint32_t GetATUMCPRegisters(void);

/**
 * @brief Get/set the cached GPIO values last written to the expanders
 * @note These functions are intended for unit testing only
 */
uint8_t GetATUMCPAOld(void);
uint8_t GetATUMCPBOld(void);
uint8_t GetATUMCPGPOld(void);
void SetATUMCPAOld(uint8_t value);
void SetATUMCPBOld(uint8_t value);
void SetATUMCPGPOld(uint8_t value);

#endif // ATUBOARD_H
