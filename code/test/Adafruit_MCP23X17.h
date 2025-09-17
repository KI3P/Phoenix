// Mock version to enable test harness to compile

#include <cstdint>
#include "Wire.h"

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
    uint16_t readGPIOA() { return 0; }
    uint16_t readGPIOB() { return 0; }
};
