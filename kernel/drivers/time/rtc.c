#include "rtc.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA 0x71
#define CMOS_STATUS_A 0x0a
#define CMOS_STATUS_B 0x0b
#define CMOS_SECOND 0x00
#define CMOS_MINUTE 0x02
#define CMOS_HOUR 0x04
#define CMOS_DAY 0x07
#define CMOS_MONTH 0x08
#define CMOS_YEAR 0x09
#define CMOS_CENTURY 0x32
#define CMOS_UPDATE_IN_PROGRESS 0x80
#define CMOS_BINARY_MODE 0x04
#define CMOS_24_HOUR 0x02

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t index) {
    uint8_t nmi_state = in8(CMOS_INDEX) & 0x80U;
    out8(CMOS_INDEX, (uint8_t)(nmi_state | index));
    return in8(CMOS_DATA);
}

static uint8_t decode(uint8_t value, uint8_t status_b) {
    if ((status_b & CMOS_BINARY_MODE) != 0) return value;
    return (uint8_t)((value & 0x0fU) + ((value >> 4) * 10U));
}

static int valid(const rtc_datetime_t *value) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    uint8_t month_days;
    return value && value->year >= 1970 && value->year <= 9999 &&
           value->month >= 1 && value->month <= 12 && value->day >= 1 &&
           value->hour <= 23 && value->minute <= 59 && value->second <= 59 &&
           (month_days = days[value->month - 1] +
                         (value->month == 2 &&
                          (value->year % 4 == 0 &&
                           (value->year % 100 != 0 || value->year % 400 == 0)))) &&
           value->day <= month_days;
}

int rtc_read_datetime(rtc_datetime_t *datetime) {
    if (!datetime) return 0;
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if ((cmos_read(CMOS_STATUS_A) & CMOS_UPDATE_IN_PROGRESS) == 0) break;
        __asm__ volatile ("pause" ::: "memory");
        if (attempt == 99999) return 0;
    }
    uint8_t status_b = cmos_read(CMOS_STATUS_B);
    uint8_t raw_hour = cmos_read(CMOS_HOUR);
    rtc_datetime_t value = {
        .year = (uint16_t)(decode(cmos_read(CMOS_YEAR), status_b) + 2000U),
        .month = decode(cmos_read(CMOS_MONTH), status_b),
        .day = decode(cmos_read(CMOS_DAY), status_b),
        .hour = decode((uint8_t)(raw_hour & 0x7fU), status_b),
        .minute = decode(cmos_read(CMOS_MINUTE), status_b),
        .second = decode(cmos_read(CMOS_SECOND), status_b)
    };
    if ((status_b & CMOS_24_HOUR) == 0 && (raw_hour & 0x80U) != 0)
        value.hour = (uint8_t)((value.hour % 12U) + 12U);
    uint8_t century = decode(cmos_read(CMOS_CENTURY), status_b);
    if (century >= 19 && century <= 99)
        value.year = (uint16_t)(century * 100U +
                                decode(cmos_read(CMOS_YEAR), status_b));
    if (!valid(&value)) return 0;
    *datetime = value;
    return 1;
}
