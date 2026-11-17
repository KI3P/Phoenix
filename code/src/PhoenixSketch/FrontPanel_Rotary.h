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

#ifndef FRONTPANEL_ROTARY_H
#define FRONTPANEL_ROTARY_H
#include "SDT.h"

/**
 * @brief Quadrature decoder for one front-panel rotary encoder, fed from the MCP23017
 *
 * The two contacts are read together as a 2-bit state, bit 0 = contact A and
 * bit 1 = contact B, 1 = open (pulled up). The encoder rests with both contacts
 * open (0b11), and one detent runs through all four states:
 *   A leads: 11 -> 10 -> 00 -> 01 -> 11
 *   B leads: 11 -> 01 -> 00 -> 10 -> 11
 * A step is counted only when the state returns to 11 after passing through the
 * whole sequence in order (Ben Buxton's full-step state table). A contact that
 * chatters as it closes or opens only moves the decoder back and forth between
 * two neighbouring states, so it can never add a step.
 *
 * Each encoder keeps its own state. The decoder is given the current state of
 * both contacts on every change, so a reading that misses an edge is corrected
 * by the next one.
 *
 * When both contacts have changed since the last reading (a fast spin, with the
 * front panel read every 1 ms), the skipped state is inferred from the
 * direction already in progress, and a jump from rest straight to both contacts
 * closed resumes the direction of the last step. One contact chattering can
 * only change one bit at a time, so neither turns chatter into steps.
 */
class Rotary_V12 {
public:
  /**
   * @brief Construct a rotary encoder decoder
   * @param reversed true to swap the sign of the steps reported by process()
   * @note Not reversed: a detent where contact A leads counts +1, B leads -1
   */
  Rotary_V12(bool reversed);

  /**
   * @brief Give the decoder the current state of both contacts
   * @param state bit 0 = contact A, bit 1 = contact B (1 = open)
   * @note Called from the front-panel service whenever either contact changes
   */
  void update(unsigned char state);

  /**
   * @brief Read and reset the accumulated steps
   * @return Steps since the last call: positive or negative by direction, 0 for none
   * @note Interrupt-safe; call from the front-panel service after update()
   */
  int process();

private:
  unsigned char _state;
  unsigned char _lastNext;  // R_x_NEXT of the last step's direction, or R_START
  int value;
  bool _reversed;
};

#endif