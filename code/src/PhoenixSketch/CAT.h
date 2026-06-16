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

#ifndef CAT_H
#define CAT_H
#include "SDT.h"

/**
 * @brief Poll and process CAT (Computer Aided Transceiver) serial commands
 * @note Called from main loop to handle Kenwood TS-480 protocol commands
 * @note Reads from SerialUSB1, buffers until semicolon terminator, parses and executes
 * @note Supports frequency control, mode changes, power settings, and debugging commands
 * @note Requires Tools->USB Type set to Dual Serial in Arduino IDE
 */
void CheckForCATSerialEvents(void);
#endif // CAT_H

