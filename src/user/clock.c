/**
 * clock: background application showing the RTC time at the bottom-right
 * corner of the screen (Book 4.3.3). Runs as a separate process; the shell
 * stays interactive while this program updates the display every second.
 *
 * Uses SYS_WRITE_AT so it writes directly at a fixed screen position without
 * touching the shared text cursor (safe when several clocks run at once).
 */

#include <stdint.h>
#include "header/user/syscall.h"
#include "header/driver/cmos.h"

#define CLOCK_ROW 24
#define CLOCK_COL 62

static uint32_t syscall3(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t result;
    __asm__ volatile("int $0x30"
                     : "=a"(result)
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
    return result;
}

static void print_time(const struct RTCTime *time) {
    char text[10];
    text[0] = '[';
    text[1] = (char) ('0' + time->hour / 10);
    text[2] = (char) ('0' + time->hour % 10);
    text[3] = ':';
    text[4] = (char) ('0' + time->minute / 10);
    text[5] = (char) ('0' + time->minute % 10);
    text[6] = ':';
    text[7] = (char) ('0' + time->second / 10);
    text[8] = (char) ('0' + time->second % 10);
    text[9] = ']';

    uint32_t position = ((uint32_t) CLOCK_ROW << 8) | CLOCK_COL;
    uint32_t color_and_length = (0x0Eu << 8) | sizeof(text);
    syscall3(SYS_WRITE_AT, position, (uint32_t) text, color_and_length);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    struct RTCTime time;
    uint8_t last_second = 0xFF;

    while (1) {
        syscall3(SYS_GET_TIME, (uint32_t) &time, 0, 0);
        if (time.second != last_second) {
            last_second = time.second;
            print_time(&time);
        }

        // Cheap busy delay so the RTC is not polled too aggressively.
        // The scheduler preempts this loop every timer tick anyway.
        for (volatile uint32_t i = 0; i < 20000; i++) {
            // spin
        }
    }

    return 0;
}
