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

void digitalWrite(uint16_t pin, uint8_t val);
uint8_t digitalRead(uint16_t pin);
void pinMode(uint16_t pin, uint8_t val);

void StartMillis(void);
void AddMillisTime(uint64_t delta_ms);
void SetMillisTime(uint64_t time_ms);

void cli(void);
void sei(void);
void delayMicroseconds(uint32_t usec);
int64_t millis(void);

#define OUTPUT 1

#include <cstdio>

class String;

class SerialClass {
public:
    std::vector<std::string> lines;
    void createFile(const char* filename);
    void closeFile();
    void print(const char*);
    void println(const char*);
    void print(int);
    void println(int);
    void print(float);
    void println(float);
    void println(const String& s);
private:
    FILE* file = nullptr;
};

extern SerialClass Serial;

class String {
public:
    // Constructors and Destructor
    String();
    String(const char* c_str);
    String(const String& other);
    String(int val);
    String(long val);
    String(float val);
    ~String();

    // Member functions
    size_t length() const;
    const char* c_str() const;

    // Operator overloads
    String& operator=(const String& other);
    String operator+(const String& other) const;
    String operator+(const char* other) const;

private:
    char* _data;
};

#endif
