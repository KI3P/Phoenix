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
unsigned long maxStretch_ms = 80; // 80ms = 12.5 Hz
unsigned long stretch_ms;

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
  if (millis() - stretch_ms > maxStretch_ms ) // If the encoder is rotating faster than maxStretch_ms, 'value' will increment CW until the encoder slows down.
    cw_fall = false;

  if ((!cw_fall) && (state == 0b10)) // cw_fall is set to TRUE when phase A interrupt is triggered
    cw_fall = true;

  if (ccw_fall && (state == 0b00)) { // if ccw_fall is already true from a previous phase B trigger, a CCW event will be triggered
    cw_fall = false;
    stretch_ms = millis();
    if (_reversed) value++;
    else value--;
  }
}

FASTRUN
void Rotary_V12::updateB(unsigned char state) {
  if (millis() - stretch_ms > maxStretch_ms ) // If the encoder is rotating faster than maxStretch_ms, 'value' will increment CCW until the encoder slows down.
    ccw_fall = false;

  if ((!ccw_fall) && (state == 0b01)) // ccw_fall is set to TRUE when phase B interrupt is triggered
    ccw_fall = true;

  if (cw_fall && (state == 0b00)) { // if cw_fall is already true from a previous phase A trigger, a CW event will be triggered
    ccw_fall = false;
    stretch_ms = millis();
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
