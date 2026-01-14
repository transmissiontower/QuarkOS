#include "drivers/rtc.h"
#include "util/util.h"

#define TIMEZONE_OFFSET 2

#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

int get_update_in_progress_flag() {
    out_port_B(CMOS_ADDRESS, 0x0A);
    return (in_port_B(CMOS_DATA) & 0x80);
}

uint8_t get_rtc_register(int reg) {
    out_port_B(CMOS_ADDRESS, reg);
    return in_port_B(CMOS_DATA);
}

void read_rtc(Time *t) {
    uint8_t last_second;
    uint8_t last_minute;
    uint8_t last_hour;
    uint8_t last_day;
    uint8_t last_month;
    uint8_t last_year;
    uint8_t register_B;

    while (get_update_in_progress_flag())
        ;

    t->second = get_rtc_register(0x00);
    t->minute = get_rtc_register(0x02);
    t->hour = get_rtc_register(0x04);
    t->day = get_rtc_register(0x07);
    t->month = get_rtc_register(0x08);
    t->year = get_rtc_register(0x09);

    do {
        last_second = t->second;
        last_minute = t->minute;
        last_hour = t->hour;
        last_day = t->day;
        last_month = t->month;
        last_year = t->year;

        while (get_update_in_progress_flag())
            ;

        t->second = get_rtc_register(0x00);
        t->minute = get_rtc_register(0x02);
        t->hour = get_rtc_register(0x04);
        t->day = get_rtc_register(0x07);
        t->month = get_rtc_register(0x08);
        t->year = get_rtc_register(0x09);
    } while ((last_second != t->second) || (last_minute != t->minute) || (last_hour != t->hour) || (last_day != t->day)
        || (last_month != t->month) || (last_year != t->year));

    register_B = get_rtc_register(0x0B);

    if (!(register_B & 0x04)) {
        t->second = (t->second & 0x0F) + ((t->second / 16) * 10);
        t->minute = (t->minute & 0x0F) + ((t->minute / 16) * 10);
        t->hour = ((t->hour & 0x0F) + (((t->hour & 0x70) / 16) * 10)) | (t->hour & 0x80);
        t->day = (t->day & 0x0F) + ((t->day / 16) * 10);
        t->month = (t->month & 0x0F) + ((t->month / 16) * 10);
        t->year = (t->year & 0x0F) + ((t->year / 16) * 10);
    }

    if (!(register_B & 0x02) && (t->hour & 0x80)) {
        t->hour = ((t->hour & 0x7F) + 12) % 24;
    }

    t->year += 2000;
    t->hour += TIMEZONE_OFFSET;

    if (t->hour >= 24) {
        t->hour -= 24;
        t->day++;
    }
}