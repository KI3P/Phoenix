// Mock version of TimeLib.h for testing
#ifndef TimeLib_h
#define TimeLib_h

#include <stdint.h>
#include <time.h>

// Mock time functions that return current system time
inline int hour() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_hour;
}

inline int minute() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_min;
}

inline int second() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_sec;
}

inline int day() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_mday;
}

inline int month() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_mon + 1;
}

inline int year() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_year + 1900;
}

#endif
