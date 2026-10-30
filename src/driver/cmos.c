#include "header/driver/cmos.h"
#include "header/cpu/portio.h"
#include <stdbool.h>

#define CMOS_REG_SECOND   0x00
#define CMOS_REG_MINUTE   0x02
#define CMOS_REG_HOUR     0x04
#define CMOS_REG_DAY      0x07
#define CMOS_REG_MONTH    0x08
#define CMOS_REG_YEAR     0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_INDEX_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static bool cmos_is_updating(void) {
    return (cmos_read(CMOS_REG_STATUS_A) & 0x80) != 0;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t) ((value & 0x0F) + ((value >> 4) * 10));
}

void cmos_read_rtc(struct RTCTime *out) {
    if (out == NULL) return;

    while (cmos_is_updating()) {
        // Wait until the RTC finishes its current update cycle
    }

    uint8_t second = cmos_read(CMOS_REG_SECOND);
    uint8_t minute = cmos_read(CMOS_REG_MINUTE);
    uint8_t hour   = cmos_read(CMOS_REG_HOUR);
    uint8_t day    = cmos_read(CMOS_REG_DAY);
    uint8_t month  = cmos_read(CMOS_REG_MONTH);
    uint8_t year   = cmos_read(CMOS_REG_YEAR);
    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);

    if (!(status_b & 0x04)) { // binary mode not set -> values are BCD
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        day    = bcd_to_binary(day);
        month  = bcd_to_binary(month);
        year   = bcd_to_binary(year);
    }

    // 12-hour mode: convert to 24-hour
    if (!(status_b & 0x02)) {
        uint8_t h = hour & 0x7F;
        if (!(status_b & 0x04)) h = bcd_to_binary(h);
        if (hour & 0x80) h = (uint8_t) ((h % 12) + 12);
        else             h = (uint8_t) (h % 12);
        hour = h;
    } else {
        if (!(status_b & 0x04)) hour = bcd_to_binary(hour);
    }

    out->second = second;
    out->minute = minute;
    out->hour   = hour;
    out->day    = day;
    out->month  = month;
    out->year   = (uint16_t) (2000 + year);
}
