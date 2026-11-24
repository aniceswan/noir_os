/**
 * clock: a small background user program that proves multitasking works.
 * It keeps drawing the current time (HH:MM:SS, read from the CMOS RTC through
 * a system call) at the top right corner of the screen while the shell stays
 * usable.
 *
 * Run it with "clock &" or "exec clock &", stop it with "kill <pid>".
 */

#include <stdint.h>
#include <stdbool.h>
#include "header/driver/cmos.h"
#include "header/user/syscall.h"

#define SCREEN_WIDTH 80
#define CLOCK_LENGTH 8                             /* "HH:MM:SS" */
#define CLOCK_ROW    0
#define CLOCK_COL    (SCREEN_WIDTH - CLOCK_LENGTH)
#define CLOCK_COLOR  0x1F                          /* White text on blue background */

static uint32_t syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t result;
    __asm__ volatile("int $0x30"
                     : "=a"(result)
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
    return result;
}

static void put_two_digits(char *out, uint8_t value) {
    out[0] = (char) ('0' + (value / 10) % 10);
    out[1] = (char) ('0' + value % 10);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    struct Time now;
    char text[CLOCK_LENGTH];

    // Redraw continuously: the display follows the RTC within the same
    // second, and comes back right away after the screen scrolls or clears
    while (true) {
        syscall(SYS_GET_TIME, (uint32_t) &now, 0, 0);

        put_two_digits(&text[0], now.hour);
        text[2] = ':';
        put_two_digits(&text[3], now.minute);
        text[5] = ':';
        put_two_digits(&text[6], now.second);

        syscall(SYS_WRITE_AT, (uint32_t) text, CLOCK_LENGTH,
                SYS_WRITE_AT_POSITION(CLOCK_ROW, CLOCK_COL, CLOCK_COLOR));
    }
    return 0;
}
