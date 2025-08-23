// Mock version of the Wire.h program to enable compilation of the test code

#ifndef Wire_h
#define Wire_h

#include <cstdint>
#include <cstddef> // For size_t

class TwoWire {
public:
    TwoWire() {}
    ~TwoWire() {}

    void begin() {}
    void beginTransmission(uint8_t address) {}
    size_t write(uint8_t data) { return 1; }
    uint8_t endTransmission(void) { return 0; }
    uint8_t requestFrom(uint8_t address, size_t quantity) { return 0; }
    int read(void) { return -1; }
    int available(void) { return 0; }
};

extern TwoWire Wire;

#endif
