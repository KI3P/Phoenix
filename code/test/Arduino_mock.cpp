#include "Arduino.h"
#include <iostream>
#include <string>

SerialClass Serial;

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

void digitalWrite(uint16_t pin, uint8_t val){}
uint8_t digitalRead(uint16_t pin){return 0;}
void pinMode(uint16_t pin, uint8_t val){}


// A mock C++ class that mimics the Arduino String class
class String {
public:
    // Default constructor
    String() : _data(new char[1]) {
        _data[0] = '\0';
    }

    // Constructor from C-style string
    String(const char* c_str) {
        if (c_str) {
            _data = new char[strlen(c_str) + 1];
            strcpy(_data, c_str);
        } else {
            _data = new char[1];
            _data[0] = '\0';
        }
    }

    // Copy constructor
    String(const String& other) {
        _data = new char[other.length() + 1];
        strcpy(_data, other._data);
    }

    // Destructor to free dynamically allocated memory
    ~String() {
        delete[] _data;
    }

    // Get the length of the string
    size_t length() const {
        return strlen(_data);
    }

    // Get the C-style string pointer
    const char* c_str() const {
        return _data;
    }

    // Overloaded assignment operator
    String& operator=(const String& other) {
        if (this != &other) {
            delete[] _data;
            _data = new char[other.length() + 1];
            strcpy(_data, other._data);
        }
        return *this;
    }

    // Overloaded concatenation operator
    String operator+(const String& other) const {
        char* new_data = new char[length() + other.length() + 1];
        strcpy(new_data, _data);
        strcat(new_data, other._data);
        String result(new_data);
        delete[] new_data;
        return result;
    }

    String operator+(const char* other) const{}

private:
    char* _data;
};