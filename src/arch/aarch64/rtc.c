#include "arch/aarch64/platform.h"
#include "drivers/rtc.h"

void drv_rtc_read_datetime(Time *time) {
    uint32_t seconds = mmio_read32(ARM_MB_VIRT(0x09010000));
    uint32_t days = seconds / 86400;
    uint32_t remainder = seconds % 86400;
    time->hour = (uint8_t)(remainder / 3600);
    time->minute = (uint8_t)((remainder % 3600) / 60);
    time->second = (uint8_t)(remainder % 60);

    uint32_t year = 1970;
    for (;;) {
        uint32_t year_days = 365;
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
            year_days = 366;
        if (days < year_days)
            break;
        days -= year_days;
        year++;
    }
    time->year = year;

    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    uint8_t month = 0;
    for (; month < 12; month++) {
        uint32_t days_in_month = month_days[month];
        if (month == 1 && leap)
            days_in_month = 29;
        if (days < days_in_month)
            break;
        days -= days_in_month;
    }
    time->month = month + 1;
    time->day = days + 1;
}
