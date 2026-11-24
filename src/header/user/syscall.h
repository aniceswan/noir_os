#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>

/*
 * Shared system call numbers for INT 0x30.
 * Kept in one place so the kernel dispatcher and all user programs agree.
 *
 * Convention: eax = service, ebx/ecx/edx = arguments (or pointers).
 * Results come back through pointer arguments. eax is 0, or -1 for a
 * service the kernel does not implement.
 *
 * Process management services return their result in eax:
 *   SYS_EXIT   (ebx = exit code)                       never returns
 *   SYS_EXEC   (ebx = absolute path, ecx = argv,
 *               edx = 1 for background)                new pid, -1 on failure
 *   SYS_KILL   (ebx = pid)                             1 when a process was terminated
 *   SYS_PS     (ebx = text buffer, ecx = buffer size)  process count
 *   SYS_GETPID                                         pid of the caller
 *
 * Services used by the clock program:
 *   SYS_GET_TIME (ebx = struct Time*, see header/driver/cmos.h)
 *   SYS_WRITE_AT (ebx = text, ecx = length, edx = SYS_WRITE_AT_POSITION(...))
 *                write text at a screen position without moving the cursor
 */

// Pack row, column and color attribute (background << 4 | foreground) for SYS_WRITE_AT
#define SYS_WRITE_AT_POSITION(row, col, color) \
    ((uint32_t) (row) | ((uint32_t) (col) << 8) | ((uint32_t) (color) << 16))


#define SYS_READ          0
#define SYS_READ_DIR      1
#define SYS_WRITE         2
#define SYS_DELETE        3
#define SYS_GETCHAR       4
#define SYS_PUTCHAR       5
#define SYS_PUTS          6
#define SYS_ACTIVATE_KB   7
#define SYS_FILE_SIZE     8
#define SYS_CLEAR_SCREEN  9
#define SYS_EXIT          10
#define SYS_EXEC          11
#define SYS_KILL          12
#define SYS_PS            13
#define SYS_GET_TIME      14
#define SYS_SET_CURSOR    15
#define SYS_GET_CURSOR    16
#define SYS_GETPID        17
#define SYS_WRITE_AT      18

#endif
