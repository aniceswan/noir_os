/**
 * echo: separate shell command (Book 4.3.6 bonus).
 * Linked as an external executable in /bin and started through PATH lookup.
 * Receives argc/argv from crt0.s and prints its arguments separated by spaces.
 */

#include <stdint.h>
#include "header/user/syscall.h"

#define TEXT_COLOR 0x0F /* White on black */

static uint32_t syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t result;
    __asm__ volatile("int $0x30"
                     : "=a"(result)
                     : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
                     : "memory");
    return result;
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) syscall(SYS_PUTCHAR, ' ', TEXT_COLOR, 0);
        syscall(SYS_PUTS, (uint32_t) argv[i], str_len(argv[i]), TEXT_COLOR);
    }
    syscall(SYS_PUTCHAR, '\n', TEXT_COLOR, 0);
    return 0;
}
