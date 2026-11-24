#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"

#define PROCESS_NAME_LENGTH_MAX          32
#define PROCESS_COUNT_MAX                16

/**
 * User virtual memory layout (4 KiB pages, one page table per process):
 *
 *   0x00000000  executable image (text, rodata, data, bss), at most 1 MiB
 *   ...         unmapped
 *   0x003F0000  user call stack, grows down from PROCESS_USER_STACK_TOP
 *   0x00400000  PROCESS_USER_STACK_TOP, end of the user address space
 *
 * The top of the stack holds the program arguments, see PROCESS_USER_INITIAL_ESP.
 */
#define PROCESS_EXECUTABLE_SIZE_MAX      0x100000u
#define PROCESS_USER_STACK_TOP           0x400000u
#define PROCESS_USER_STACK_PAGE_COUNT    16u

// 256 frames for a 1 MiB executable image plus the user stack frames
#define PROCESS_PAGE_FRAME_COUNT_MAX     (PROCESS_EXECUTABLE_SIZE_MAX / PAGE_FRAME_SIZE + PROCESS_USER_STACK_PAGE_COUNT)

/**
 * Program arguments. The kernel copies them to the top of the new user stack:
 *
 *   [PROCESS_USER_STACK_TOP - PROCESS_ARG_BUFFER_SIZE, PROCESS_USER_STACK_TOP)  argument strings
 *   below that                                    argv[0 .. PROCESS_ARG_COUNT_MAX], NULL terminated
 *   [esp + 4]                                     char **argv
 *   [esp + 0]                                     int argc        <- PROCESS_USER_INITIAL_ESP
 *
 * crt0.s only needs to "call main" to hand argc and argv over to main().
 */
#define PROCESS_ARG_COUNT_MAX            8
#define PROCESS_ARG_BUFFER_SIZE          256
#define PROCESS_USER_ARG_STRING_ADDR     (PROCESS_USER_STACK_TOP - PROCESS_ARG_BUFFER_SIZE)
#define PROCESS_USER_ARGV_ADDR           (PROCESS_USER_ARG_STRING_ADDR - (PROCESS_ARG_COUNT_MAX + 1) * 4)
#define PROCESS_USER_INITIAL_ESP         (PROCESS_USER_ARGV_ADDR - 8)

#define CPU_EFLAGS_BASE_FLAG               0x2
#define CPU_EFLAGS_FLAG_CARRY              0x1
#define CPU_EFLAGS_FLAG_PARITY             0x4
#define CPU_EFLAGS_FLAG_AUX_CARRY          0x10
#define CPU_EFLAGS_FLAG_ZERO               0x40
#define CPU_EFLAGS_FLAG_SIGN               0x80
#define CPU_EFLAGS_FLAG_TRAP               0x100
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE   0x200
#define CPU_EFLAGS_FLAG_DIRECTION          0x400
#define CPU_EFLAGS_FLAG_OVERFLOW           0x800
#define CPU_EFLAGS_FLAG_IO_PRIVILEGE       0x3000
#define CPU_EFLAGS_FLAG_NESTED_TASK        0x4000
#define CPU_EFLAGS_FLAG_MODE               0x8000
#define CPU_EFLAGS_FLAG_RESUME             0x10000
#define CPU_EFLAGS_FLAG_VIRTUAL_8086       0x20000
#define CPU_EFLAGS_FLAG_ALIGNMENT_CHECK    0x40000
#define CPU_EFLAGS_FLAG_VINTERRUPT_FLAG    0x80000
#define CPU_EFLAGS_FLAG_VINTERRUPT_PENDING 0x100000
#define CPU_EFLAGS_FLAG_CPUID_INSTRUCTION  0x200000
#define CPU_EFLAGS_FLAG_AES_SCHEDULE_LOAD  0x40000000
#define CPU_EFLAGS_FLAG_ALTER_INSTRUCTION  0x80000000

// Return code constant for process_create_user_process()
#define PROCESS_CREATE_SUCCESS                   0
#define PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED 1
#define PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT   2
#define PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY    3
#define PROCESS_CREATE_FAIL_FS_READ_FAILURE      4

// PID 0 is never handed out, it means "no process"
#define PROCESS_PID_NONE                         0

// Init process: the shell, stored in the root directory (inode 1) of the file system
#define PROCESS_INIT_NAME                        "shell"
#define PROCESS_INIT_PARENT_INODE                1u

/**
 * Contain information needed for task to be able to get interrupted and resumed later
 *
 * @param cpu                         All CPU register state
 * @param eip                         CPU instruction counter to resume execution
 * @param eflags                      Flag register to load before resuming the execution
 * @param page_directory_virtual_addr CPU register CR3, containing pointer to active page directory
 */
struct Context {
    struct CPURegister   cpu;
    uint32_t             eip;
    uint32_t             eflags;
    struct PageDirectory *page_directory_virtual_addr;
} __attribute__((packed));

/**
 * Process state used by the scheduler.
 *
 * READY   : waiting in the queue for its CPU time slice
 * RUNNING : currently owns the CPU (at most one process)
 * BLOCKED : waiting for a foreground child process to terminate
 */
typedef enum PROCESS_STATE {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
} PROCESS_STATE;

/**
 * Structure data containing information about a process
 *
 * @param metadata Process metadata, contain various information about process
 * @param context  Process context used for context saving & switching
 * @param memory   Memory used for the process
 */
struct ProcessControlBlock {
    struct {
        bool          active;
        uint32_t      pid;
        uint32_t      parent_pid;
        uint32_t      waiting_pid;  // Child PID this process is blocked on, PROCESS_PID_NONE otherwise
        PROCESS_STATE state;
        char          name[PROCESS_NAME_LENGTH_MAX];
    } metadata;

    struct Context context;

    struct {
        void     *virtual_addr_used[PROCESS_PAGE_FRAME_COUNT_MAX];
        uint32_t page_frame_used_count;
    } memory;
};

/**
 * Program arguments in kernel memory, copied into the new process' stack.
 *
 * @param argc   Number of arguments, at most PROCESS_ARG_COUNT_MAX
 * @param offset Offset of each null-terminated argument inside buffer
 * @param buffer Argument strings
 */
struct ProcessArguments {
    uint32_t argc;
    uint32_t offset[PROCESS_ARG_COUNT_MAX];
    char     buffer[PROCESS_ARG_BUFFER_SIZE];
};

/**
 * Global process manager state
 *
 * @param active_process_count Number of active entries in _process_list
 * @param last_pid             Last PID handed out, the next process gets last_pid + 1
 * @param init_pid             PID of the init process (the shell)
 */
struct ProcessManagerState {
    uint32_t active_process_count;
    uint32_t last_pid;
    uint32_t init_pid;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
extern struct ProcessManagerState process_manager_state;

/**
 * Get currently running process PCB pointer
 *
 * @return Will return NULL if there's no running process
 */
struct ProcessControlBlock* process_get_current_running_pcb_pointer(void);

/**
 * Get an active process by its PID
 *
 * @return Will return NULL if there's no active process with that PID
 */
struct ProcessControlBlock* process_get_pcb_by_pid(uint32_t pid);

/**
 * Create new user process and setup the virtual address space.
 * All available return code is defined with macro "PROCESS_CREATE_*"
 *
 * @note          This procedure assumes no reentrancy in ISR
 * @param request Appropriate read request for the executable
 * @return        Process creation return code
 */
int32_t process_create_user_process(struct EXT2DriverRequest request);

/**
 * Same as process_create_user_process() but also passes program arguments
 * and reports the new PID.
 *
 * @param request Appropriate read request for the executable
 * @param args    Program arguments in kernel memory, NULL for no argument
 * @param pid_out Will be filled with the new PID on success, may be NULL
 * @return        Process creation return code
 */
int32_t process_create_user_process_with_args(
    struct EXT2DriverRequest request,
    const struct ProcessArguments *args,
    uint32_t *pid_out
);

/**
 * Create the init process (the shell). Called once at boot, and again by
 * the scheduler whenever the init process is gone.
 *
 * @return Process creation return code
 */
int32_t process_create_init_process(void);

/**
 * Get the foreground process: the process that another process (usually the
 * shell) is blocked on. This is the target of Ctrl+C.
 *
 * @return PID of the foreground process, PROCESS_PID_NONE if there is none
 */
uint32_t process_get_foreground_pid(void);

/**
 * Write the list of active processes as a text table, one line per process
 * with its PID, parent PID, state and name. The result is null-terminated.
 *
 * @param buffer      Destination buffer
 * @param buffer_size Destination buffer size in bytes
 * @return            Number of active processes
 */
uint32_t process_get_list_text(char *buffer, uint32_t buffer_size);

/**
 * Destroy process then release page directory and process control block
 *
 * @param pid Process ID to delete
 * @return    True if process destruction success
 */
bool process_destroy(uint32_t pid);

#endif
