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

/**
 * Object for interacting with AD7991 I2C ADC chip
 */
class AD7991 {
public:
  AD7991();
  bool begin(uint8_t i2c_addr, TwoWire *theWire = &Wire);
  uint16_t readADCsingle(uint8_t ch);
  
private:
  Adafruit_I2CDevice *i2c_dev; // I2C device
};

#endif // #ifndef AD7991_h
