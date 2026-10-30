#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

/*
 * Shared system call numbers for INT 0x30.
 * Kept in one place so the kernel dispatcher and all user programs agree.
 *
 * Convention: eax = service, ebx/ecx/edx = arguments (or pointers).
 * The kernel returns a value in eax for the services that need one.
 *
 * SYS_WRITE_AT: ebx = (row << 8) | col, ecx = char* buffer,
 *               edx = (color << 8) | length
 * Writes directly at a screen position without moving the text cursor,
 * which lets background programs (clock) display without disturbing others.
 */

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
