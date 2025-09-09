#include "Arduino.h"
#include <iostream>
#include <string>

SerialClass Serial;
SerialClass SerialUSB1;

#define NUMPINS 41
static bool pin_mode[NUMPINS];
static bool pin_value[NUMPINS];

void SerialClass::createFile(const char* filename) {
    if (filename != nullptr) {
        file = fopen(filename, "w");
    }
}

void SerialClass::closeFile() {
    if (file != nullptr) {
        fclose(file);
        file = nullptr;
    }
}

void SerialClass::print(const char* s) {
    if (file) {
        fprintf(file, "%s", s);
    } else {
        std::cout << s;
    }
}

void SerialClass::println(const char* s) {
    if (file) {
        fprintf(file, "%s\n", s);
    } else {
        std::cout << s << std::endl;
    }
}

void SerialClass::print(int n) {
    if (file) {
        fprintf(file, "%d", n);
    } else {
        std::cout << n;
    }
}

void SerialClass::println(int n) {
    if (file) {
        fprintf(file, "%d\n", n);
    } else {
        std::cout << n << std::endl;
    }
}

void SerialClass::print(float f) {
    if (file) {
        fprintf(file, "%f", f);
    } else {
        std::cout << f;
    }
}

void SerialClass::println(float f) {
    if (file) {
        fprintf(file, "%f\n", f);
    } else {
        std::cout << f << std::endl;
    }
}

void SerialClass::println(const String& s) {
    if (file) {
        fprintf(file, "%s\n", s.c_str());
    } else {
        std::cout << s.c_str() << std::endl;
    }
}

uint32_t SerialClass::available(void) {
    if (readIndex >= inputBuffer.size()) {
        return 0;
    }
    return inputBuffer.size() - readIndex;
}

uint8_t SerialClass::read(void) {
    if (readIndex >= inputBuffer.size()) {
        return 0;
    }
    return inputBuffer[readIndex++];
}

uint32_t SerialClass::availableForWrite(void) {
    return 0;
}

void SerialClass::flush(void) {}

void SerialClass::feedData(const char* data) {
    if (data != nullptr) {
        while (*data) {
            inputBuffer.push_back(static_cast<uint8_t>(*data));
            data++;
        }
    }
}

void SerialClass::clearBuffer(void) {
    inputBuffer.clear();
    readIndex = 0;
}

void SerialClass::println() {
    if (file) {
        fprintf(file, "\n");
    } else {
        std::cout << std::endl;
    }
}

int64_t tstart;

void cli(void){}
void sei(void){}
void delayMicroseconds(uint32_t usec){}

void StartMillis(void){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    tstart = (int64_t)(1000 * tv.tv_sec) + (int64_t)((float32_t)tv.tv_usec/1000);
}

void AddMillisTime(uint64_t delta_ms){
    tstart -= delta_ms;
}

int64_t millis(void){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)(1000 * tv.tv_sec) + (int64_t)((float32_t)tv.tv_usec/1000) - tstart;
}

void SetMillisTime(uint64_t time_ms){
    tstart = millis()+tstart - time_ms;
}

void digitalWrite(uint16_t pin, uint8_t val){
    //digital_write_pins.push_back(pin);
    //digital_write_values.push_back(val);
    if (pin < NUMPINS) pin_value[pin] = val;
}
uint8_t digitalRead(uint16_t pin){
    if (pin < NUMPINS) return pin_value[pin];
    return 0;
}

void pinMode(uint16_t pin, uint8_t val){
    if (pin < NUMPINS) pin_mode[pin] = val;
}

uint8_t getPinMode(uint16_t pin){
    if (pin < NUMPINS) return pin_mode[pin];
    return 0;
}


#include <cstring>
// A mock C++ class that mimics the Arduino String class
String::String() : _data(new char[1]) {
    _data[0] = '\0';
}

// Constructor from C-style string
String::String(const char* c_str) {
    if (c_str) {
        _data = new char[strlen(c_str) + 1];
        strcpy(_data, c_str);
    } else {
        _data = new char[1];
        _data[0] = '\0';
    }
}

String::String(int val) {
    char buf[12];
    sprintf(buf, "%d", val);
    _data = new char[strlen(buf) + 1];
    strcpy(_data, buf);
}

String::String(long val) {
    char buf[22];
    sprintf(buf, "%ld", val);
    _data = new char[strlen(buf) + 1];
    strcpy(_data, buf);
}

String::String(float val) {
    char buf[32];
    sprintf(buf, "%f", val);
    _data = new char[strlen(buf) + 1];
    strcpy(_data, buf);
}


// Copy constructor
String::String(const String& other) {
    _data = new char[other.length() + 1];
    strcpy(_data, other._data);
}

// Destructor to free dynamically allocated memory
String::~String() {
    delete[] _data;
}

// Get the length of the string
size_t String::length() const {
    return strlen(_data);
}

// Get the C-style string pointer
const char* String::c_str() const {
    return _data;
}

// Overloaded assignment operator
String& String::operator=(const String& other) {
    if (this != &other) {
        delete[] _data;
        _data = new char[other.length() + 1];
        strcpy(_data, other._data);
    }
    return *this;
}

// Overloaded concatenation operator
String String::operator+(const String& other) const {
    char* new_data = new char[length() + other.length() + 1];
    strcpy(new_data, _data);
    strcat(new_data, other._data);
    String result(new_data);
    delete[] new_data;
    return result;
}

String String::operator+(const char* other) const{
    char* new_data = new char[length() + strlen(other) + 1];
    strcpy(new_data, _data);
    strcat(new_data, other);
    String result(new_data);
    delete[] new_data;
    return result;
}

void SetLPFBand(int band){}
void SetBPFBand(int band){}

void flush(void){}