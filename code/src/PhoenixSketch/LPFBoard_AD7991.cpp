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

/*
  Arduino library for AD7991, a 4-Channel, 12-Bit ADC with I2C Compatible Interface in 8-Lead SOT-23 
 */

#include "LPFBoard_AD7991.h"
#include "Arduino.h"

/**
 * @brief Construct a new AD7991 object.
 *
 * Initializes the i2c_dev member variable to nullptr.
 */
AD7991::AD7991() { i2c_dev = nullptr; }

/**
 * @brief Initialize the AD7991 chip.
 *
 * @param i2c_addr I2C address of the AD7991 chip.
 * @param theWire Pointer to the TwoWire object.
 * @return true if initialization is successful, false otherwise.
 */
bool AD7991::begin(uint8_t i2c_addr, TwoWire *theWire) {
  if (i2c_dev) {
    delete i2c_dev;
  }

  i2c_dev = new Adafruit_I2CDevice(i2c_addr, theWire);
  lastChannel = AD7991_NO_CHANNEL;

  return i2c_dev->begin();
}

/**
 * @brief Read ADC value in single-ended mode.
 *
 * When the channel differs from the previous conversion's, the channel is
 * first converted getDiscardCount() times (default 2) and those results are
 * thrown away. That costs that many extra I2C transactions per channel switch.
 * See the class comment in LPFBoard_AD7991.h for why.
 *
 * @param ch Channel to read from (0-2).
 * @return ADC value; 0 for an invalid channel; 65535 (-1) if the I2C
 *         transaction failed; 65534 (-2) if the wrong channel was returned.
 */
uint16_t AD7991::readADCsingle(uint8_t ch) {
  if (ch > 2) {
    return 0; // Invalid channel
  }
  if (ch != lastChannel) {
    for (uint8_t i = 0; i < discardCount; i++) {
      uint16_t discarded = convert(ch);
      if (discarded >= 0xFFFE) {
        return discarded;    // the I2C error codes, -1 and -2
      }
    }
  }
  return convert(ch);
}

/**
 * @brief One conversion of a channel, selected in the same I2C transaction.
 *
 * @param ch Channel to convert (0-2), already checked by the caller.
 * @return ADC value, or the error codes listed under readADCsingle().
 */
uint16_t AD7991::convert(uint8_t ch) {
  if (i2c_dev == nullptr) {
    return -1;
  }

  uint8_t commandByte = 0;

  // Command byte is:
  // D7 D6 D5 D4 D3 D2 D1 D0
  // D4=ch0, D5=ch1, D6=Ch2

  if (ch==0) {
    commandByte = REGISTER_SETUP | (0b0001 << 4);
  }
  if (ch==1) {
    commandByte = REGISTER_SETUP | (0b0010 << 4);
  }
  if (ch==2) {
    commandByte = REGISTER_SETUP | (0b0100 << 4);
  }
  
  uint8_t adcValue[2];
  if (!i2c_dev->write_then_read(&commandByte, 1, adcValue, 2)) {
    lastChannel = AD7991_NO_CHANNEL; // the ADC's selection is now unknown
    return -1; // Write-then-read failed
  }
  lastChannel = ch;

  // Confirm that the returned channel identifier matches what we specified
  // bits 5 and 4 of the first returned byte are the channel idenfier
  uint8_t CHid = (0b00110000 & adcValue[0]) >> 4;
  if (CHid != ch){
    Serial.print("Wrong channel! Expected ");
    Serial.print(ch);
    Serial.print(". Received ");
    Serial.println(CHid);
    lastChannel = AD7991_NO_CHANNEL; // the ADC's selection is now unknown
    return -2; // wrong channel returned
  }
  return ((0b00001111 & adcValue[0]) << 8) | adcValue[1];
}
