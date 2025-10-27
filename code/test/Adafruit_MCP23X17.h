// Mock version to enable test harness to compile

#include <cstdint>
#include "Wire.h"

// MCP23X17-specific constants
// (Arduino constants like CHANGE, INPUT_PULLUP, LOW, HIGH are defined in Arduino.h)
#define MCP23XXX_INT_ERR 255

class Adafruit_MCP23X17 {
public:
    Adafruit_MCP23X17() {}
    ~Adafruit_MCP23X17() {}

    bool begin(uint8_t addr = 0, TwoWire *theWire = nullptr) { return true; }
    bool begin_I2C(uint8_t addr = 0){ return true; }
    bool begin_I2C(uint8_t addr, void *theWire){ return true; }
    void enableAddrPins() {}
    void pinMode(uint8_t pin, uint8_t mode) {}
    void digitalWrite(uint8_t pin, uint8_t value) {}
    uint8_t digitalRead(uint8_t pin) { return 0; }
    void writeGPIOA(uint8_t value) {gpioval = (gpioval & 0x00FF) | (value << 8);}
    void writeGPIOB(uint8_t value) {gpioval = (gpioval & 0xFF00) | (value);}
    void writeGPIOAB(uint16_t value) {gpioval = value;}
    uint8_t readGPIOA() { return (uint8_t)((gpioval >> 8) & 0x00FF); }
    uint8_t readGPIOB() { return (uint8_t)((gpioval) & 0x00FF);; }
    uint16_t readGPIOAB() { return gpioval; }
    void setupInterruptPin(uint8_t pin, uint8_t mode) {}
    uint8_t getLastInterruptPin() { return MCP23XXX_INT_ERR; }
    void clearInterrupts() {}
    void setupInterrupts(bool mirror, bool openDrain, uint8_t polarity) {}
private:
    uint16_t gpioval;
};
