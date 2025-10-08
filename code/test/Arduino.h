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

// Mock elapsedMicros class (Teensy-specific)
class elapsedMicros {
public:
    elapsedMicros(void) { _start = micros(); }
    elapsedMicros(uint32_t val) { _start = micros() - val; }
    elapsedMicros(const elapsedMicros &orig) { _start = orig._start; }
    operator uint32_t() const { return micros() - _start; }
    elapsedMicros& operator=(const elapsedMicros &rhs) { _start = rhs._start; return *this; }
    elapsedMicros& operator=(uint32_t val) { _start = micros() - val; return *this; }
    elapsedMicros& operator-=(uint32_t val) { _start += val; return *this; }
    elapsedMicros& operator+=(uint32_t val) { _start -= val; return *this; }
    elapsedMicros operator-(int val) const { elapsedMicros r(*this); r._start += val; return r; }
    elapsedMicros operator-(uint32_t val) const { elapsedMicros r(*this); r._start += val; return r; }
    elapsedMicros operator+(int val) const { elapsedMicros r(*this); r._start -= val; return r; }
    elapsedMicros operator+(uint32_t val) const { elapsedMicros r(*this); r._start -= val; return r; }
private:
    uint32_t _start;
};

#define OUTPUT 1
#define HEX 16
#define BIN 2

// Teensy 4.1 temperature monitor registers (mock)
// Use mock variables instead of hardware addresses to avoid segfault
extern uint32_t mock_TEMPMON_TEMPSENSE0;
extern uint32_t mock_TEMPMON_TEMPSENSE1;
extern uint32_t mock_HW_OCOTP_ANA1;

#define TEMPMON_TEMPSENSE0 mock_TEMPMON_TEMPSENSE0
#define TEMPMON_TEMPSENSE1 mock_TEMPMON_TEMPSENSE1
#define HW_OCOTP_ANA1 mock_HW_OCOTP_ANA1
#define TMS0_POWER_DOWN_MASK 0x1
#define TMS1_MEASURE_FREQ(x) (x)
#define TEMPMON_ROOMTEMP 25

#include <cstdio>
#include <cstdlib>

// PROGMEM macro for storing data in flash (mock - just stores in RAM)
#define PROGMEM

// Adafruit GFX font structures
typedef struct {
    uint16_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    uint8_t xAdvance;
    int8_t xOffset;
    int8_t yOffset;
} GFXglyph;

typedef struct {
    uint8_t *bitmap;
    GFXglyph *glyph;
    uint8_t first;
    uint8_t last;
    uint8_t yAdvance;
} GFXfont;

// itoa function (not standard in C++)
inline char* itoa(int value, char* str, int base) {
    if (base == 10) {
        sprintf(str, "%d", value);
    } else if (base == 16) {
        sprintf(str, "%x", value);
    } else if (base == 2) {
        int i = 0;
        int temp = value;
        if (temp == 0) {
            str[i++] = '0';
        } else {
            while (temp > 0) {
                str[i++] = (temp % 2) ? '1' : '0';
                temp /= 2;
            }
            // Reverse the string
            for (int j = 0; j < i/2; j++) {
                char c = str[j];
                str[j] = str[i-j-1];
                str[i-j-1] = c;
            }
        }
        str[i] = '\0';
    }
    return str;
}

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
