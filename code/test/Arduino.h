// Mock version of the Arduino.h program to enable compilation of the test code

#ifndef Arduino_h
#define Arduino_h

#include <stdint.h>
#include <sys/time.h>
#include <cstddef>
#include <string>
#include <vector>

typedef float float32_t;
#define AudioInterrupts()
#define DMAMEM
#define FASTRUN
#define DEC 10
#define HEX 16

void digitalWrite(uint16_t pin, uint8_t val);
uint8_t digitalRead(uint16_t pin);
void pinMode(uint16_t pin, uint8_t val);
uint8_t getPinMode(uint16_t pin);

void StartMillis(void);
void AddMillisTime(uint64_t delta_ms);
void SetMillisTime(uint64_t time_ms);

void cli(void);
void __disable_irq(void);
void __enable_irq(void);
void sei(void);
void delayMicroseconds(uint32_t usec);
int64_t millis(void);
uint32_t micros(void);
void MyDelay(unsigned long millisWait);

#define OUTPUT 1
#define HEX 16
#define BIN 2

#include <cstdio>

class String;

class SerialClass {
public:
    std::vector<std::string> lines;
    void createFile(const char* filename);
    void closeFile();
    void print(const char*);
    void println(const char*);
    void println(void);
    void print(int);
    void println(int);
    void print(int64_t);
    void println(int64_t);
    void print(uint32_t);
    void println(uint32_t);
    void print(size_t);
    void println(size_t);
    void print(float);
    void println(float);
    void println(const String& s);
    void printf(const char* format, ...);
    uint32_t available(void);
    uint8_t read(void);
    uint32_t availableForWrite(void);
    void flush(void);
    void feedData(const char* data);
    void clearBuffer(void);
private:
    FILE* file = nullptr;
    std::vector<uint8_t> inputBuffer;
    size_t readIndex = 0;
};

extern SerialClass Serial;
extern SerialClass SerialUSB1;

class String {
public:
    // Constructors and Destructor
    String();
    String(const char* c_str);
    String(const String& other);
    String(int val);
    String(int val, int base);
    String(unsigned int val);
    String(unsigned int val, int base);
    String(long val);
    String(long val, int base);
    String(float val);
    ~String();

    // Member functions
    size_t length() const;
    const char* c_str() const;

    // Operator overloads
    String& operator=(const String& other);
    String operator+(const String& other) const;
    String operator+(const char* other) const;
    String& operator+=(const String& other);
    String& operator+=(const char* other);
    String substring(unsigned int from, unsigned int to) const;

private:
    char* _data;
};

// Free function to allow const char* + String
String operator+(const char* left, const String& right);

// F() macro for storing strings in flash memory (mock just returns the string)
#define F(str) str

#endif
