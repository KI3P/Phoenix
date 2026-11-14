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
 * @file SDT.h
 * @brief Minimal stand-in for PhoenixSketch/SDT.h, for the standalone ATU test sketch
 *
 * ATUBoard.cpp includes "SDT.h" exactly as the other Phoenix hardware modules do.
 * This file supplies just enough of the real header for it to compile here, so
 * ATUBoard.cpp/.h can later be copied into PhoenixSketch/ without change.
 *
 * When integrating into Phoenix:
 *  - the ATU bit map below goes into PhoenixSketch/SDT.h next to the existing
 *    hardwareRegister bit map (bits 0-32 are already used there)
 *  - ATU_MCP23017_ADDR / ATU_MCP23008_ADDR go into PhoenixSketch/Config.h
 *  - the two ATU_*_present flags are added to struct BIT in PhoenixSketch/SDT.h
 *  - everything else in this file already exists in PhoenixSketch/SDT.h
 */

#ifndef SDT_H
#define SDT_H

#include <Arduino.h>
#include <Wire.h>
#include <arm_math.h>            // float32_t
#include <Adafruit_MCP23X17.h>
#include <Adafruit_MCP23X08.h>

///////////////////////////////////////////////////////////////////////////////
// Already in PhoenixSketch/SDT.h
///////////////////////////////////////////////////////////////////////////////

typedef int errno_t;
#define ESUCCESS             0
#define ENOI2C              -1
#define EFAIL               -10

#define Debug(x) Serial.println(x)

// The 64 bit register that records the state of the radio hardware
extern uint64_t hardwareRegister;

// Macros used to manipulate the hardware register
#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1ULL)
#define SET_BIT(byte, bit) ((byte) |= (1ULL << (bit)));buffer_add()
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1ULL << (bit)));buffer_add()
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1ULL << (bit)));buffer_add()

// Every time the value of hardwareRegister is updated, store this in a rolling buffer
void buffer_add(void);

///////////////////////////////////////////////////////////////////////////////
// To be added to PhoenixSketch/SDT.h: ATU part of the hardwareRegister bit map
///////////////////////////////////////////////////////////////////////////////

#define ATULLSB         33  // L1 (K1)  ... MCP23017 GPA0
#define ATULMSB         40  // L8 (K8)  ... MCP23017 GPA7
#define ATUCLSB         41  // C1 (K9)  ... MCP23017 GPB0
#define ATUCMSB         48  // C8 (K16) ... MCP23017 GPB7
#define ATUCAPINPUTBIT  49  // K17, schematic "LC-CL":   MCP23008 GP0
#define ATUBALANCEDBIT  50  // K18, schematic "BAL OUT": MCP23008 GP1
#define ATUNETWORKBIT   51  // K19+K20, schematic "BYPASS": MCP23008 GP2

///////////////////////////////////////////////////////////////////////////////
// To be added to PhoenixSketch/Config.h: ATU I2C addresses (on Wire2)
///////////////////////////////////////////////////////////////////////////////

#define ATU_MCP23017_ADDR 0x20  // U5: close J4, J5, J6 (A0=A1=A2=0)
#define ATU_MCP23008_ADDR 0x21  // U6: close J8, J9, leave J7 open (A0=1, A1=A2=0)

///////////////////////////////////////////////////////////////////////////////
// Cut-down struct BIT: in Phoenix these two fields are added to the real one
///////////////////////////////////////////////////////////////////////////////

struct BIT {
    bool ATU_MCP23017_present;
    bool ATU_MCP23008_present;
};
extern struct BIT bit_results;

#endif // SDT_H
