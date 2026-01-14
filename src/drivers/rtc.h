#ifndef RTC_H
#define RTC_H

#include "lib/stdint.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} Time;

void read_rtc(Time *t);

#endif