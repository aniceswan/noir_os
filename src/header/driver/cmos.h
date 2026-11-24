#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>

/**
 * CMOS / RTC (Real-Time Clock) driver.
 * This header only depends on stdint.h so user programs can share struct Time.
 */

// The register index goes to the address port, its value is read from the data port
#define CMOS_ADDRESS_PORT        0x70
#define CMOS_DATA_PORT           0x71

// Bit 7 of the address port masks NMI, keep it set while talking to the CMOS
#define CMOS_NMI_DISABLE         0x80

// RTC registers
#define CMOS_REGISTER_SECOND     0x00
#define CMOS_REGISTER_MINUTE     0x02
#define CMOS_REGISTER_HOUR       0x04
#define CMOS_REGISTER_DAY        0x07
#define CMOS_REGISTER_MONTH      0x08
#define CMOS_REGISTER_YEAR       0x09
#define CMOS_REGISTER_STATUS_A   0x0A
#define CMOS_REGISTER_STATUS_B   0x0B

// Status register A: the RTC is updating its registers, values may be inconsistent
#define CMOS_STATUS_A_UPDATING   0x80
// Status register B: data format of the RTC registers
#define CMOS_STATUS_B_24_HOUR    0x02
#define CMOS_STATUS_B_BINARY     0x04
// In 12 hour mode, bit 7 of the hour register marks PM
#define CMOS_HOUR_PM_FLAG        0x80

// The RTC year register only stores the last two digits
#define CMOS_YEAR_BASE           2000

/**
 * Wall-clock time read from the RTC, in 24 hour format.
 */
struct Time {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} __attribute__((packed));

/**
 * Read the current time from the CMOS RTC
 *
 * @param time Will be filled with the current time
 */
void cmos_read_time(struct Time *time);

#endif
