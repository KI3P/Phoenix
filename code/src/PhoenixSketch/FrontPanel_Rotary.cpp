/* Rotary encoder handler for arduino.
 *
 * Copyright 2011 Ben Buxton. Licenced under the GNU GPL Version 3.
 * Contact: bb@cactii.net
 *
 * Modified by John Melton (G0ORX) to allow feeding A/B pin states
 * to allow working with MCP23017.
 *
 */

#include "FrontPanel_Rotary.h"

static bool ccw_fall = 0;
static bool cw_fall = 0;

/*
 * Constructor. Each arg is the pin number for each encoder contact.
 */
Rotary_V12::Rotary_V12(bool reversed) {
  _reversed = reversed;
  cw_fall = false;
  ccw_fall = false;
}

FASTRUN
void Rotary_V12::updateA(unsigned char state) {
  if ((!cw_fall) && (state == 0b10))  // cw_fall is set to TRUE when phase A interrupt is triggered
    cw_fall = true;

  if (ccw_fall && (state == 0b00)) {  // if ccw_fall is already set to true from a previous B phase trigger, the ccw event will be triggered
    cw_fall = false;
    ccw_fall = false;
    if (_reversed) value++;
    else value--;
  }
}

FASTRUN
void Rotary_V12::updateB(unsigned char state) {
  if ((!ccw_fall) && (state == 0b01))  //ccw leading edge is true
    ccw_fall = true;

  if (cw_fall && (state == 0b00)) {  //cw trigger
    cw_fall = false;
    ccw_fall = false;
    if (_reversed) value--;
    else value++;
  }
}

FASTRUN
int Rotary_V12::process() {
  __disable_irq();
  int result = value;
  value = 0;
  __enable_irq();
  return result;
}
