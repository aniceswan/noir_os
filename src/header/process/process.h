#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"

#define PROCESS_NAME_LENGTH_MAX          32
#define PROCESS_PAGE_FRAME_COUNT_MAX     512
#define PROCESS_COUNT_MAX                16

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

/* User virtual memory layout used by process creation (see process.c) */
#define USER_STACK_PAGE_COUNT 4u
#define USER_STACK_TOP        0x400000u

/**
 * Context, contain information needed for a task to be able to get
 * interrupted and resumed later.
 *
 * @param cpu                         All CPU register state
 * @param eip                         CPU instruction counter to resume execution
 * @param eflags                      Flag register to load before resuming execution
 * @param page_directory_virtual_addr CPU register CR3, active page directory
 */
struct Context {
    struct CPURegister     cpu;
    uint32_t               eip;
    uint32_t               eflags;
    struct PageDirectory  *page_directory_virtual_addr;
};

/**
 * Process state, used by the scheduler to decide which process runs next.
 */
typedef enum PROCESS_STATE {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_TERMINATED,
} PROCESS_STATE;

struct ProcessMetadata {
    uint32_t      pid;
    char          name[PROCESS_NAME_LENGTH_MAX];
    PROCESS_STATE state;
};

struct ProcessControlBlock {
    struct ProcessMetadata metadata;
    struct Context         context;
    struct {
        void     *virtual_addr_used[PROCESS_PAGE_FRAME_COUNT_MAX];
        uint32_t  page_frame_used_count;
    } memory;
};

struct ProcessManagerState {
    uint32_t active_process_count;
    uint32_t current_process_index;
    uint32_t next_pid;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
extern struct ProcessManagerState process_manager_state;

/**
 * Get currently running process PCB pointer
 *
 * @return Will return NULL if there's no running process
 */
struct ProcessControlBlock *process_get_current_running_pcb_pointer(void);

/**
 * Create new user process and setup the virtual address space.
 *
 * @param request EXT2 read request describing the executable
 * @return        PROCESS_CREATE_* code
 */
int32_t process_create_user_process(struct EXT2DriverRequest request);

/**
 * Create a new user process with an explicit argv vector. argv[0] is
 * normally the program name. Strings are copied into the new process stack.
 *
 * @param request EXT2 read request describing the executable
 * @param argc    Argument count (1..16)
 * @param argv    Argument string vector
 * @return        PROCESS_CREATE_* code
 */
int32_t process_create_user_process_args(
    struct EXT2DriverRequest request,
    int argc,
    const char *const argv[]
);

/**
 * Destroy process then release page directory and process control block
 *
 * @param pid Process ID to delete
 * @return    True if process destruction success
 */
bool process_destroy(uint32_t pid);

#endif
