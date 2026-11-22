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

/**
 * @file ATUBoard.cpp
 * @brief K9HZ 100W antenna tuner (ATU) control
 *
 * The tuner is two boards. The RF board is an L network: eight series inductors
 * and eight shunt capacitors switched by relays, plus relays that move the
 * capacitor bank between the input and output sides, select a balanced (4:1
 * balun) output, and bypass the whole network. The control board drives those
 * relays from two I2C I/O expanders through UDN2987 high-side drivers.
 *
 * Hardware Interface:
 * -------------------
 * - MCP23017 (U5) at ATU_MCP23017_ADDR: GPA -> inductors, GPB -> capacitors
 * - MCP23008 (U6) at ATU_MCP23008_ADDR: GP0-GP2 -> mode relays
 * - Both on the BANDS connector, I2C bus Wire2 (shared with LPF and BPF boards)
 * - UDN2987 drivers are active high: GPIO = 1 puts +12 V on the relay coil.
 *   Every relay de-energised is the safe state (network bypassed).
 * - Control board J1/J2/J3 connect pin-for-pin to RF board J5/J6/J7.
 *
 * Register Layout (bits of the global hardwareRegister):
 * -------------------------------------------------------
 * Bit    Pin        Relay  Energised (1)                   De-energised (0)
 * 33-40  U5 GPA0-7  K1-K8  L1-L8 in series                 inductor shorted
 * 41-48  U5 GPB0-7  K9-K16 C1-C8 shunt to ground           capacitor open
 * 49     U6 GP0     K17    caps at input side (C-L)        caps at output side (L-C)
 * 50     U6 GP1     K18    output to 4:1 balun             output to RF OUTPUT coax
 * 51     U6 GP2     K19+20 L/C network in line             network bypassed
 *
 * L1-L8 = 0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8 uH
 * C1-C8 = 10, 22, 47, 100, 220, 470, 1000, 2000 pF (C8 is C8 and C9 in parallel)
 *
 * The schematic labels the K19/K20 control line "BYPASS", but energising it
 * takes the network OUT of bypass: K19/K20 route through their NC contacts
 * when de-energised. ATUNETWORKBIT is named for what it does.
 *
 * Relationship to Phoenix:
 * ------------------------
 * This is a standalone copy for the TestATUBoard sketch. The master copy is
 * code/src/PhoenixSketch/ATUBoard.cpp, which adds SetATUState() and
 * ATUChangeAllowed() and is compiled only when Config.h defines ATU_PRESENT.
 * Make driver fixes there first.
 */

#include "SDT.h"
#include "ATUBoard.h"

static Adafruit_MCP23X17 mcpATU_LC;   // U5: inductors and capacitors
static Adafruit_MCP23X08 mcpATU_Ctl;  // U6: mode relays
static bool ATUinitialized = false;
static errno_t ATUerrno = EFAIL;
static uint8_t mcpA_old = 0x00;
static uint8_t mcpB_old = 0x00;
static uint8_t mcpGP_old = 0x00;

#define ATU_REGISTER_WIDTH 19
#define ATU_REGISTER_MASK  ((1ULL << ATU_REGISTER_WIDTH) - 1)

#define ATU_GPA_STATE (uint8_t)((hardwareRegister >> ATULLSB) & 0xFF)
#define ATU_GPB_STATE (uint8_t)((hardwareRegister >> ATUCLSB) & 0xFF)
#define ATU_GP_STATE  (uint8_t)((hardwareRegister >> ATUCAPINPUTBIT) & 0x07)

// 64-bit masks throughout: a 32-bit literal mask would clear bits 32-63
#define SET_ATU_L(val) (hardwareRegister = (hardwareRegister & ~(0xFFULL << ATULLSB)) | (((uint64_t)(val) & 0xFFULL) << ATULLSB));buffer_add()
#define SET_ATU_C(val) (hardwareRegister = (hardwareRegister & ~(0xFFULL << ATUCLSB)) | (((uint64_t)(val) & 0xFFULL) << ATUCLSB));buffer_add()
#define SET_ATU_STATE(val) (hardwareRegister = (hardwareRegister & ~(ATU_REGISTER_MASK << ATULLSB)) | (((uint64_t)(val) & ATU_REGISTER_MASK) << ATULLSB));buffer_add()

static const float32_t inductorValues_uH[ATU_NUM_INDUCTORS] =
    {0.05f, 0.1f, 0.22f, 0.45f, 1.0f, 2.2f, 4.4f, 8.8f};
static const float32_t capacitorValues_pF[ATU_NUM_CAPACITORS] =
    {10.0f, 22.0f, 47.0f, 100.0f, 220.0f, 470.0f, 1000.0f, 2000.0f};

///////////////////////////////////////////////////////////////////////////////
// Unit Testing Helper Functions
///////////////////////////////////////////////////////////////////////////////

/**
 * Get the ATU part of hardwareRegister (bits 33-51), shifted down to bit 0.
 *
 * @return 19-bit ATU state
 */
uint32_t GetATURegisterState(void){
    return (uint32_t)((hardwareRegister >> ATULLSB) & ATU_REGISTER_MASK);
}

/**
 * Set the ATU part of hardwareRegister without updating the hardware.
 *
 * @param value 19-bit ATU state (higher bits ignored)
 */
void SetATURegisterState(uint32_t value){
    SET_ATU_STATE(value);
}

uint8_t GetATUMCPAOld(void){ return mcpA_old; }
uint8_t GetATUMCPBOld(void){ return mcpB_old; }
uint8_t GetATUMCPGPOld(void){ return mcpGP_old; }
void SetATUMCPAOld(uint8_t value){ mcpA_old = value; }
void SetATUMCPBOld(uint8_t value){ mcpB_old = value; }
void SetATUMCPGPOld(uint8_t value){ mcpGP_old = value; }

/**
 * Read the expander outputs back over I2C. Returns the actual hardware state,
 * not the cached software state.
 *
 * @return Bits 0-7 GPA (inductors), 8-15 GPB (capacitors), 16-23 MCP23008 GP
 */
uint32_t GetATUMCPRegisters(void){
    uint32_t ab = mcpATU_LC.readGPIOAB();       // A in the low byte, B in the high byte
    uint32_t gp = mcpATU_Ctl.readGPIO();
    return (ab & 0xFFFF) | ((gp & 0xFF) << 16);
}

///////////////////////////////////////////////////////////////////////////////
// Initialization and hardware update
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the two I/O expanders on the ATU control board.
 *
 * 1. Sets the ATU bits of hardwareRegister to the safe state (all relays off)
 * 2. Starts I2C communication with the MCP23017 and MCP23008 on Wire2
 * 3. Configures every GPIO pin as an output
 * 4. Writes the safe state to the hardware
 * 5. Updates the BIT (Built-In Test) results
 *
 * Subsequent calls return ESUCCESS without re-initializing once it has
 * succeeded. A failed initialization is not latched, so it can be retried.
 *
 * @return ESUCCESS if both expanders responded, ENOI2C otherwise
 */
errno_t InitializeATUBoard(void){
    if (ATUinitialized) return ATUerrno;

    // Safe state: network bypassed, no L, no C, caps at output, unbalanced
    SET_ATU_STATE(0);

    bit_results.ATU_MCP23017_present = false;
    bit_results.ATU_MCP23008_present = false;
    ATUerrno = ESUCCESS;

    if (mcpATU_LC.begin_I2C(ATU_MCP23017_ADDR, &Wire2)){
        for (int i = 0; i < 16; i++){
            mcpATU_LC.pinMode(i, OUTPUT);
        }
        mcpA_old = ATU_GPA_STATE;
        mcpB_old = ATU_GPB_STATE;
        mcpATU_LC.writeGPIOA(ATU_GPA_STATE);
        mcpATU_LC.writeGPIOB(ATU_GPB_STATE);
        bit_results.ATU_MCP23017_present = true;
    } else {
        Debug("ATU MCP23017 not found at 0x"+String(ATU_MCP23017_ADDR,HEX));
        ATUerrno = ENOI2C;
    }

    if (mcpATU_Ctl.begin_I2C(ATU_MCP23008_ADDR, &Wire2)){
        for (int i = 0; i < 8; i++){
            mcpATU_Ctl.pinMode(i, OUTPUT);
        }
        mcpGP_old = ATU_GP_STATE;
        mcpATU_Ctl.writeGPIO(ATU_GP_STATE);
        bit_results.ATU_MCP23008_present = true;
    } else {
        Debug("ATU MCP23008 not found at 0x"+String(ATU_MCP23008_ADDR,HEX));
        ATUerrno = ENOI2C;
    }

    if (ATUerrno == ESUCCESS){
        Debug("ATU board initialized");
        ATUinitialized = true;
    }
    return ATUerrno;
}

/**
 * Update the expander outputs if they differ from the cached values.
 *
 * Write-on-change keeps I2C traffic (and relay activity) to a minimum.
 * Called after any change to the ATU bits of hardwareRegister.
 */
void UpdateATURegisters(void){
    if (mcpA_old != ATU_GPA_STATE){
        mcpATU_LC.writeGPIOA(ATU_GPA_STATE);
        mcpA_old = ATU_GPA_STATE;
    }
    if (mcpB_old != ATU_GPB_STATE){
        mcpATU_LC.writeGPIOB(ATU_GPB_STATE);
        mcpB_old = ATU_GPB_STATE;
    }
    if (mcpGP_old != ATU_GP_STATE){
        mcpATU_Ctl.writeGPIO(ATU_GP_STATE);
        mcpGP_old = ATU_GP_STATE;
    }
}

/**
 * Return the tuner to the safe state: every relay de-energised.
 */
void ResetATUBoard(void){
    SET_ATU_STATE(0);
    UpdateATURegisters();
}

///////////////////////////////////////////////////////////////////////////////
// Inductor and capacitor selection
///////////////////////////////////////////////////////////////////////////////

void SetATUInductors(uint8_t mask){
    SET_ATU_L(mask);
    UpdateATURegisters();
}

uint8_t GetATUInductors(void){
    return ATU_GPA_STATE;
}

void SetATUCapacitors(uint8_t mask){
    SET_ATU_C(mask);
    UpdateATURegisters();
}

uint8_t GetATUCapacitors(void){
    return ATU_GPB_STATE;
}

void SetATULC(uint8_t lMask, uint8_t cMask){
    SET_ATU_L(lMask);
    SET_ATU_C(cMask);
    UpdateATURegisters();
}

float32_t ATUInductorValue_uH(uint8_t n){
    if ((n < 1) || (n > ATU_NUM_INDUCTORS)) return 0.0f;
    return inductorValues_uH[n-1];
}

float32_t ATUCapacitorValue_pF(uint8_t n){
    if ((n < 1) || (n > ATU_NUM_CAPACITORS)) return 0.0f;
    return capacitorValues_pF[n-1];
}

float32_t GetATUInductance_uH(void){
    float32_t total = 0.0f;
    uint8_t mask = ATU_GPA_STATE;
    for (uint8_t i = 0; i < ATU_NUM_INDUCTORS; i++){
        if (mask & (1 << i)) total += inductorValues_uH[i];
    }
    return total;
}

float32_t GetATUCapacitance_pF(void){
    float32_t total = 0.0f;
    uint8_t mask = ATU_GPB_STATE;
    for (uint8_t i = 0; i < ATU_NUM_CAPACITORS; i++){
        if (mask & (1 << i)) total += capacitorValues_pF[i];
    }
    return total;
}

///////////////////////////////////////////////////////////////////////////////
// Mode relays
///////////////////////////////////////////////////////////////////////////////

void ATUSelectCapsAtInput(void){
    SET_BIT(hardwareRegister, ATUCAPINPUTBIT);
    UpdateATURegisters();
}

void ATUSelectCapsAtOutput(void){
    CLEAR_BIT(hardwareRegister, ATUCAPINPUTBIT);
    UpdateATURegisters();
}

bool ATUCapsAtInput(void){
    return GET_BIT(hardwareRegister, ATUCAPINPUTBIT);
}

void ATUSelectBalancedOutput(void){
    SET_BIT(hardwareRegister, ATUBALANCEDBIT);
    UpdateATURegisters();
}

void ATUSelectUnbalancedOutput(void){
    CLEAR_BIT(hardwareRegister, ATUBALANCEDBIT);
    UpdateATURegisters();
}

bool ATUBalancedOutput(void){
    return GET_BIT(hardwareRegister, ATUBALANCEDBIT);
}

void ATUEngageNetwork(void){
    SET_BIT(hardwareRegister, ATUNETWORKBIT);
    UpdateATURegisters();
}

void ATUBypassNetwork(void){
    CLEAR_BIT(hardwareRegister, ATUNETWORKBIT);
    UpdateATURegisters();
}

bool ATUNetworkEngaged(void){
    return GET_BIT(hardwareRegister, ATUNETWORKBIT);
}

///////////////////////////////////////////////////////////////////////////////
// Individual relay access by schematic number
///////////////////////////////////////////////////////////////////////////////

/**
 * PRIVATE: Map a relay number (K1-K20) to its hardwareRegister bit.
 *
 * @return The bit number, or -1 if k is out of range
 */
static int8_t RelayToBit(uint8_t k){
    if ((k >= 1) && (k <= 8))  return ATULLSB + (k - 1);
    if ((k >= 9) && (k <= 16)) return ATUCLSB + (k - 9);
    if (k == 17) return ATUCAPINPUTBIT;
    if (k == 18) return ATUBALANCEDBIT;
    if ((k == 19) || (k == 20)) return ATUNETWORKBIT;
    return -1;
}

/**
 * Energise or de-energise one relay. K19 and K20 share a control line, so
 * setting either one sets both.
 *
 * @param k Relay number, 1 to 20
 * @param energise true to energise the coil
 * @return ESUCCESS, or EFAIL if k is out of range
 */
errno_t SetATURelay(uint8_t k, bool energise){
    int8_t bit = RelayToBit(k);
    if (bit < 0){
        Debug("ATU: invalid relay K"+String(k));
        return EFAIL;
    }
    if (energise){
        SET_BIT(hardwareRegister, bit);
    } else {
        CLEAR_BIT(hardwareRegister, bit);
    }
    UpdateATURegisters();
    return ESUCCESS;
}

bool GetATURelay(uint8_t k){
    int8_t bit = RelayToBit(k);
    if (bit < 0) return false;
    return GET_BIT(hardwareRegister, bit);
}
