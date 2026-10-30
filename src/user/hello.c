/**
 * hello: minimal external application (Book 4.3.4).
 * Runs as a separate ring-3 process, created through the exec syscall.
 */

#include <stdint.h>
#include "header/user/syscall.h"

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30"
                     : /* <Empty> */
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static void puts_color(const char *s, uint8_t color) {
    syscall(SYS_PUTS, (uint32_t) s, str_len(s), color);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    puts_color("Hello World! (mbOSok external application)\n", 0x0B);
    puts_color("This process was started with exec and will now exit.\n", 0x0F);
    return 0;
}
