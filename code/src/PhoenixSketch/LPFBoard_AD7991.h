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

#ifndef AD7991_h
#define AD7991_h

#include <Adafruit_I2CDevice.h>

// Command byte structure:
// D7 D6 D5 D4 D3 D2 D1 D0
//   D7 to D4: [CH3 CH2 CH1 CH0] - if channel address bit is set to 1, channel is selected for conversion
//   D3: REF_SEL - if 0, the supply is used as the reference. If 1, an external reference applied to VIN3/VREF pin is used
//   D2: FLTR - if 0 filtering on SDA/SCL is enabled; if 1 filtering is bypassed. Default: 0
//   D1: Bit trial delay - default 0
//   D0: Sample delay - default 0
// We want to sample CH0 and CH1 using the external 4.096V reference
#define REGISTER_SETUP 0b00001000

// No channel converted yet: the first read always counts as a channel switch
#define AD7991_NO_CHANNEL 0xFF
// Conversions discarded after a channel switch (see the class comment)
#define AD7991_DEFAULT_DISCARDS 2
#define AD7991_MAX_DISCARDS 4

/**
 * Object for interacting with AD7991 I2C ADC chip
 *
 * Discarding conversions after a channel switch: the AD8307 detector
 * outputs feed the AD7991 directly through their 12.5 kOhm output resistance.
 * A conversion taken in the same I2C transaction as the channel switch still
 * holds part of the previous channel's voltage. On radio 1 (2026-11-01) the
 * forward reading took 14 % of the reflected voltage and the reflected reading
 * 18 % of the forward voltage. That compressed the SWR return loss to 0.65 of
 * its true value. Each conversion keeps about 15-19 % of the previous one, so
 * one discarded conversion left 2-4 % crosstalk (measured) and two leave well
 * under 1 %. By default, readADCsingle() therefore converts a newly selected
 * channel twice, discards both, and returns the third conversion. At Wire2's
 * 100 kHz each conversion takes about 0.5 ms, so a forward + reflected pair
 * takes about 3 ms.
 */
class AD7991 {
public:
  AD7991();
  bool begin(uint8_t i2c_addr, TwoWire *theWire = &Wire);
  uint16_t readADCsingle(uint8_t ch);
  void setDiscardCount(uint8_t n) { discardCount = (n > AD7991_MAX_DISCARDS) ? AD7991_MAX_DISCARDS : n; }
  uint8_t getDiscardCount(void) const { return discardCount; }

private:
  uint16_t convert(uint8_t ch);
  Adafruit_I2CDevice *i2c_dev; // I2C device
  uint8_t lastChannel = AD7991_NO_CHANNEL;   // channel of the last conversion
  uint8_t discardCount = AD7991_DEFAULT_DISCARDS;
};

#endif // #ifndef AD7991_h
