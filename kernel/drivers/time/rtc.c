#include "rtc.h"
#include "../../core/sync/spinlock.h"

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

static spinlock_t rtc_lock;

void rtc_initialize(void) { spinlock_init(&rtc_lock); }

static int decode(uint8_t value, uint8_t status_b, uint8_t *decoded) {
    if (!decoded) return 0;
    if ((status_b & CMOS_BINARY_MODE) != 0) {
        *decoded = value;
        return 1;
    }
    if ((value & 0x0fU) > 9U || (value >> 4) > 9U) return 0;
    *decoded = (uint8_t)((value & 0x0fU) + ((value >> 4) * 10U));
    return 1;
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
    uint64_t flags = spinlock_lock_irqsave(&rtc_lock);
    int success = 0;
    for (uint32_t sample = 0; sample < 3 && !success; ++sample) {
        uint8_t status_b = cmos_read(CMOS_STATUS_B);
        rtc_datetime_t readings[2];
        for (uint32_t reading = 0; reading < 2 && !success; ++reading) {
            int stable = 0;
            for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
                if ((cmos_read(CMOS_STATUS_A) & CMOS_UPDATE_IN_PROGRESS) == 0) {
                    stable = 1;
                    break;
                }
                __asm__ volatile ("pause" ::: "memory");
            }
            uint8_t year, month, day, hour, minute, second, century;
            uint8_t raw_hour = cmos_read(CMOS_HOUR);
            if (!stable || !decode(cmos_read(CMOS_YEAR), status_b, &year) ||
                !decode(cmos_read(CMOS_MONTH), status_b, &month) ||
                !decode(cmos_read(CMOS_DAY), status_b, &day) ||
                !decode((uint8_t)(raw_hour & 0x7fU), status_b, &hour) ||
                !decode(cmos_read(CMOS_MINUTE), status_b, &minute) ||
                !decode(cmos_read(CMOS_SECOND), status_b, &second) ||
                !decode(cmos_read(CMOS_CENTURY), status_b, &century))
                break;
            readings[reading] = (rtc_datetime_t){
                .year = (uint16_t)(year + 2000U), .month = month, .day = day,
                .hour = hour, .minute = minute, .second = second};
            if ((status_b & CMOS_24_HOUR) == 0 && (raw_hour & 0x80U) != 0)
                readings[reading].hour = (uint8_t)((hour % 12U) + 12U);
            if (century >= 19 && century <= 99)
                readings[reading].year = (uint16_t)(century * 100U + year);
            if (!valid(&readings[reading])) break;
            if (reading == 1 && readings[0].year == readings[1].year &&
                readings[0].month == readings[1].month &&
                readings[0].day == readings[1].day &&
                readings[0].hour == readings[1].hour &&
                readings[0].minute == readings[1].minute &&
                readings[0].second == readings[1].second) {
                *datetime = readings[1];
                success = 1;
            }
        }
    }
    spinlock_unlock_irqrestore(&rtc_lock, flags);
    return success;
}
