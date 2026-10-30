/**
 * echo: separate shell command (Book 4.3.6 bonus). Linked as an external
 * executable and started through the shell's PATH lookup, not built-in.
 * Receives argc/argv from crt0 and prints its arguments.
 */

#include <stdint.h>
#include "header/user/syscall.h"

static uint32_t syscall3(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
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
        if (i > 1) syscall3(SYS_PUTCHAR, ' ', 0x0F, 0);
        syscall3(SYS_PUTS, (uint32_t) argv[i], str_len(argv[i]), 0x0F);
    }
    syscall3(SYS_PUTCHAR, '\n', 0x0F, 0);
    return 0;
}
