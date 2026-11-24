#include <stdbool.h>
#include "header/driver/cmos.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

// Upper bound for busy-waiting on the RTC, so a broken RTC can never hang the kernel
#define CMOS_WAIT_LIMIT 100000
#define CMOS_READ_RETRY 8

static uint8_t cmos_read_register(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, CMOS_NMI_DISABLE | reg);
    return in(CMOS_DATA_PORT);
}

static void cmos_wait_until_stable(void) {
    for (uint32_t i = 0; i < CMOS_WAIT_LIMIT; i++) {
        if ((cmos_read_register(CMOS_REGISTER_STATUS_A) & CMOS_STATUS_A_UPDATING) == 0) return;
    }
}

// Read the RTC registers as they are, without any format conversion
static void cmos_read_raw(struct Time *time) {
    cmos_wait_until_stable();
    time->second = cmos_read_register(CMOS_REGISTER_SECOND);
    time->minute = cmos_read_register(CMOS_REGISTER_MINUTE);
    time->hour   = cmos_read_register(CMOS_REGISTER_HOUR);
    time->day    = cmos_read_register(CMOS_REGISTER_DAY);
    time->month  = cmos_read_register(CMOS_REGISTER_MONTH);
    time->year   = cmos_read_register(CMOS_REGISTER_YEAR);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t) ((value & 0x0F) + (value >> 4) * 10);
}

void cmos_read_time(struct Time *time) {
    // An update may start right in the middle of a read: read until two
    // consecutive results are identical
    struct Time current;
    struct Time previous;
    cmos_read_raw(&current);
    for (uint32_t i = 0; i < CMOS_READ_RETRY; i++) {
        previous = current;
        cmos_read_raw(&current);
        if (memcmp(&previous, &current, sizeof(struct Time)) == 0) break;
    }

    uint8_t status_b = cmos_read_register(CMOS_REGISTER_STATUS_B);
    bool    is_pm    = (current.hour & CMOS_HOUR_PM_FLAG) != 0;
    uint8_t hour     = current.hour & (uint8_t) ~CMOS_HOUR_PM_FLAG;
    uint8_t year     = (uint8_t) current.year;

    // RTC registers are stored in BCD unless the binary flag is set
    if ((status_b & CMOS_STATUS_B_BINARY) == 0) {
        current.second = bcd_to_binary(current.second);
        current.minute = bcd_to_binary(current.minute);
        current.day    = bcd_to_binary(current.day);
        current.month  = bcd_to_binary(current.month);
        hour           = bcd_to_binary(hour);
        year           = bcd_to_binary(year);
    }

    // Convert 12 hour clock into 24 hour: 12 AM is 00, 12 PM stays 12
    if ((status_b & CMOS_STATUS_B_24_HOUR) == 0) {
        hour = hour % 12;
        if (is_pm) hour = (uint8_t) (hour + 12);
    }

    current.hour = hour;
    current.year = (uint16_t) (CMOS_YEAR_BASE + year);
    *time = current;
}
