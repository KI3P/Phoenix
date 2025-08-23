// Mock version of the Arduino.h program to enable compilation of the test code

#ifndef Arduino_h
#define Arduino_h

#include <stdint.h>
#include <sys/time.h>
#include <cstddef>

typedef float float32_t;
#define AudioInterrupts()

void StartMillis(void);
void AddMillisTime(uint64_t delta_ms);
void SetMillisTime(uint64_t time_ms);

void cli(void);
void sei(void);
void delayMicroseconds(uint32_t usec);
int64_t millis(void);

#define OUTPUT 1

#endif
