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

    // Special handling for AD7991 ADC - extract channel from command byte
    // and include it in the response
    if (write_len >= 1 && read_len >= 2 && write_buffer != nullptr && read_buffer != nullptr) {
        // AD7991 command byte format: bits 6-4 contain channel selection (one-hot encoded)
        // Response format: bits 5-4 of first byte contain channel ID
        uint8_t commandByte = write_buffer[0];
        uint8_t channelSelect = (commandByte >> 4) & 0b0111;
        uint8_t channelId = 0;

        // Convert one-hot channel select to channel ID
        if (channelSelect == 0b0001) channelId = 0;      // CH0
        else if (channelSelect == 0b0010) channelId = 1; // CH1
        else if (channelSelect == 0b0100) channelId = 2; // CH2

        // Construct response with channel ID in bits 5-4 of first byte
        // Use mock_read_data for the actual ADC value (12-bit)
        uint16_t adcValue = 0;
        if (mock_read_length >= 2) {
            // Extract 12-bit value from mock data
            adcValue = ((mock_read_data[0] & 0x0F) << 8) | mock_read_data[1];
        }

        // Format response: first byte has channel ID in bits 5-4, high 4 bits of value in bits 3-0
        read_buffer[0] = (channelId << 4) | ((adcValue >> 8) & 0x0F);
        read_buffer[1] = adcValue & 0xFF;

        return true;
    }

    // Fall back to simple copy for other devices
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