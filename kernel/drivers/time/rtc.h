#ifndef OS_KERNEL_DRIVERS_TIME_RTC_H
#define OS_KERNEL_DRIVERS_TIME_RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_datetime_t;

int rtc_read_datetime(rtc_datetime_t *datetime);
void rtc_initialize(void);

#endif
