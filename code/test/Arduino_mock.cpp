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