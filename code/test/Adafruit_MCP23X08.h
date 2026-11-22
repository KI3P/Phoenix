// Mock version to enable test harness to compile

#ifndef ADAFRUIT_MCP23X08_MOCK_H
#define ADAFRUIT_MCP23X08_MOCK_H

#include <cstdint>
#include "Wire.h"

// Also defined (unguarded) in the Adafruit_MCP23X17.h mock
#ifndef MCP23XXX_INT_ERR
#define MCP23XXX_INT_ERR 255
#endif

class Adafruit_MCP23X08 {
public:
    Adafruit_MCP23X08() {}
    ~Adafruit_MCP23X08() {}

    bool begin_I2C(uint8_t addr = 0){ return true; }
    bool begin_I2C(uint8_t addr, void *theWire){ return true; }
    void pinMode(uint8_t pin, uint8_t mode) {}
    void digitalWrite(uint8_t pin, uint8_t value) {}
    uint8_t digitalRead(uint8_t pin) { return (gpioval >> pin) & 1; }
    // As in the real library, the port argument is ignored by the 8-bit part
    void writeGPIO(uint8_t value, uint8_t port = 0) { gpioval = value; }
    uint8_t readGPIO(uint8_t port = 0) { return gpioval; }
private:
    uint8_t gpioval = 0;
};

#endif // ADAFRUIT_MCP23X08_MOCK_H
