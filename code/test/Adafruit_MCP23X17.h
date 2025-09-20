// Mock version to enable test harness to compile

#include <cstdint>
#include "Wire.h"

// Constants for interrupt and pin modes
#define CHANGE 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
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
    void writeGPIOA(uint16_t value) {}
    void writeGPIOB(uint16_t value) {}
    void writeGPIOAB(uint16_t value) {}
    uint16_t readGPIOA() { return 0; }
    uint16_t readGPIOB() { return 0; }
    uint16_t readGPIOAB() { return 0; }
    void setupInterruptPin(uint8_t pin, uint8_t mode) {}
    uint8_t getLastInterruptPin() { return MCP23XXX_INT_ERR; }
    void clearInterrupts() {}
    void setupInterrupts(bool mirror, bool openDrain, uint8_t polarity) {}
};
