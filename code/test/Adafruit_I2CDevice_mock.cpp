// Mock implementation of Adafruit_I2CDevice for testing

#include "Adafruit_I2CDevice.h"
#include <cstring>

// Static member variables
bool Adafruit_I2CDevice::mock_device_present = true;
uint8_t Adafruit_I2CDevice::mock_read_data[16] = {0};
size_t Adafruit_I2CDevice::mock_read_length = 0;

Adafruit_I2CDevice::Adafruit_I2CDevice(uint8_t addr, TwoWire *theWire)
    : _addr(addr), _wire(theWire), _begun(false) {
}

Adafruit_I2CDevice::~Adafruit_I2CDevice() {
}

bool Adafruit_I2CDevice::begin() {
    _begun = mock_device_present;
    return _begun;
}

bool Adafruit_I2CDevice::detected() {
    return _begun && mock_device_present;
}

bool Adafruit_I2CDevice::read(uint8_t *buffer, size_t len, bool stop) {
    if (!_begun || !mock_device_present) {
        return false;
    }

    size_t copy_len = (len < mock_read_length) ? len : mock_read_length;
    if (copy_len > 0 && buffer != nullptr) {
        memcpy(buffer, mock_read_data, copy_len);
    }

    return true;
}

bool Adafruit_I2CDevice::write(const uint8_t *buffer, size_t len, bool stop, const uint8_t *prefix_buffer, size_t prefix_len) {
    if (!_begun || !mock_device_present) {
        return false;
    }

    // In a real implementation, this would write to the I2C device
    // For mock purposes, we just return success
    return true;
}

bool Adafruit_I2CDevice::write_then_read(const uint8_t *write_buffer, size_t write_len, uint8_t *read_buffer, size_t read_len, bool stop) {
    if (!_begun || !mock_device_present) {
        return false;
    }

    // Handle the read part by copying mock data
    size_t copy_len = (read_len < mock_read_length) ? read_len : mock_read_length;
    if (copy_len > 0 && read_buffer != nullptr) {
        memcpy(read_buffer, mock_read_data, copy_len);
    }

    return true;
}

bool Adafruit_I2CDevice::setSpeed(uint32_t desiredclk) {
    return _begun;
}

uint8_t Adafruit_I2CDevice::address() {
    return _addr;
}

// Test helper functions
void Adafruit_I2CDevice::setMockDevicePresent(bool present) {
    mock_device_present = present;
}

void Adafruit_I2CDevice::setMockReadData(const uint8_t* data, size_t length) {
    if (length > sizeof(mock_read_data)) {
        length = sizeof(mock_read_data);
    }
    mock_read_length = length;
    if (data != nullptr && length > 0) {
        memcpy(mock_read_data, data, length);
    }
}

void Adafruit_I2CDevice::resetMockState() {
    mock_device_present = true;
    mock_read_length = 0;
    memset(mock_read_data, 0, sizeof(mock_read_data));
}