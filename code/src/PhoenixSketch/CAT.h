#ifndef CAT_H
#define CAT_H
#include "SDT.h"

/**
 * @brief Poll and process CAT (Computer Aided Transceiver) serial commands
 * @note Called from main loop to handle Kenwood TS-2000 protocol commands
 * @note Reads from SerialUSB1, buffers until semicolon terminator, parses and executes
 * @note Supports frequency control, mode changes, power settings, and debugging commands
 * @note Requires Tools->USB Type set to Dual Serial in Arduino IDE
 */
void CheckForCATSerialEvents(void);
#endif // CAT_H

