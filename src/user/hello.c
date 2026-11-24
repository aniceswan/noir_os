/**
 * hello: the simplest external application of mbOSok.
 * It is a separate executable in the file system, linked with crt0.s and
 * user-linker.ld, and only talks to the kernel through system calls.
 *
 * Usage: "exec hello" or just "hello" (found through PATH).
 *        "hello <name>..." also greets every name given as argument.
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

static void print(const char *text) {
    uint32_t length = 0;
    while (text[length] != '\0') length++;
    syscall(SYS_PUTS, (uint32_t) text, length, TEXT_COLOR);
}

int main(int argc, char **argv) {
    print("Hello World!\n");

    // argv[0] is the program name, the rest comes from the command line
    for (int i = 1; i < argc; i++) {
        print("Hello, ");
        print(argv[i]);
        print("!\n");
    }

    // Returning from main() ends the process through the exit syscall in crt0.s
    return 0;
}
