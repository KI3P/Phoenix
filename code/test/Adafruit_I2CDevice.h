// Mock version of Adafruit_I2CDevice.h to enable compilation of the test code

#ifndef ADAFRUIT_I2CDEVICE_H
#define ADAFRUIT_I2CDEVICE_H

#include <cstdint>
#include "Wire.h"

class Adafruit_I2CDevice {
public:
    Adafruit_I2CDevice(uint8_t addr, TwoWire *theWire = &Wire);
    ~Adafruit_I2CDevice();

    bool begin();
    bool detected();
    bool read(uint8_t *buffer, size_t len, bool stop = true);
    bool write(const uint8_t *buffer, size_t len, bool stop = true, const uint8_t *prefix_buffer = nullptr, size_t prefix_len = 0);
    bool write_then_read(const uint8_t *write_buffer, size_t write_len, uint8_t *read_buffer, size_t read_len, bool stop = false);
    bool setSpeed(uint32_t desiredclk);

    uint8_t address();

private:
    uint8_t _addr;
    TwoWire *_wire;
    bool _begun;

    // Mock state for testing
    static bool mock_device_present;
    static uint8_t mock_read_data[16];
    static size_t mock_read_length;

public:
    // Test helper functions
    static void setMockDevicePresent(bool present);
    static void setMockReadData(const uint8_t* data, size_t length);
    static void resetMockState();
};

#endif // ADAFRUIT_I2CDEVICE_H