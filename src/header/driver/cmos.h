#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71

/**
 * Real Time Clock time, in binary (already BCD-converted).
 *
 * @param second 0..59
 * @param minute 0..59
 * @param hour   0..23
 * @param day    1..31
 * @param month  1..12
 * @param year   full year (e.g. 2026)
 */
struct RTCTime {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} __attribute__((packed));

/**
 * Read the current date/time from the CMOS RTC.
 *
 * @param out Output time structure
 */
void cmos_read_rtc(struct RTCTime *out);

#endif
