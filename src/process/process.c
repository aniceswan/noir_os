#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

struct ProcessManagerState process_manager_state = {
    .active_process_count = 0,
    .last_pid             = PROCESS_PID_NONE,
    .init_pid             = PROCESS_PID_NONE,
};

static uint32_t ceil_div(uint32_t numerator, uint32_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (!_process_list[i].metadata.active) return i;
    }
    return -1;
}

// PID starts from 1 and is never reused, so a stale PID can never hit a new process
static uint32_t process_generate_new_pid(void) {
    process_manager_state.last_pid++;
    return process_manager_state.last_pid;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.active &&
            _process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

struct ProcessControlBlock* process_get_pcb_by_pid(uint32_t pid) {
    if (pid == PROCESS_PID_NONE) return NULL;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.active && _process_list[i].metadata.pid == pid) {
            return &_process_list[i];
        }
    }
    return NULL;
}

/**
 * Allocate one user page frame in the active page directory and record it in the PCB
 */
static bool process_allocate_page(struct ProcessControlBlock *pcb, struct PageDirectory *page_dir, void *virtual_addr) {
    if (pcb->memory.page_frame_used_count >= PROCESS_PAGE_FRAME_COUNT_MAX) return false;
    if (!paging_allocate_user_page_frame(page_dir, virtual_addr)) return false;
    pcb->memory.virtual_addr_used[pcb->memory.page_frame_used_count] = virtual_addr;
    pcb->memory.page_frame_used_count++;
    return true;
}

/**
 * Release every page frame recorded in the PCB.
 * The process page directory must be the active one.
 */
static void process_release_pages(struct ProcessControlBlock *pcb, struct PageDirectory *page_dir) {
    for (uint32_t i = 0; i < pcb->memory.page_frame_used_count; i++) {
        paging_free_user_page_frame(page_dir, pcb->memory.virtual_addr_used[i]);
        pcb->memory.virtual_addr_used[i] = NULL;
    }
    pcb->memory.page_frame_used_count = 0;
}

/**
 * Write argc and argv to the top of the user stack, see PROCESS_USER_INITIAL_ESP.
 * The new process page directory must be the active one.
 */
static void process_setup_arguments(const struct ProcessArguments *args) {
    char     **user_argv   = (char **) PROCESS_USER_ARGV_ADDR;
    char     *user_strings = (char *) PROCESS_USER_ARG_STRING_ADDR;
    uint32_t *user_stack   = (uint32_t *) PROCESS_USER_INITIAL_ESP;
    uint32_t argc = 0;

    if (args != NULL) {
        memcpy(user_strings, args->buffer, PROCESS_ARG_BUFFER_SIZE);
        user_strings[PROCESS_ARG_BUFFER_SIZE - 1] = '\0';
        for (uint32_t i = 0; i < args->argc && i < PROCESS_ARG_COUNT_MAX; i++) {
            if (args->offset[i] >= PROCESS_ARG_BUFFER_SIZE) break;
            user_argv[argc] = user_strings + args->offset[i];
            argc++;
        }
    }
    user_argv[argc] = NULL;

    user_stack[0] = argc;
    user_stack[1] = (uint32_t) user_argv;
}

int32_t process_create_user_process_with_args(
    struct EXT2DriverRequest request,
    const struct ProcessArguments *args,
    uint32_t *pid_out
) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;

    /* --- 0. Validation --- */
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    // and stays page aligned, because the image is loaded page by page
    uint32_t entrypoint = (uint32_t) request.buf;
    if (entrypoint >= KERNEL_VIRTUAL_ADDRESS_BASE || entrypoint % PAGE_FRAME_SIZE != 0) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // The request name may live in the caller's user memory, which becomes
    // unreachable once the new page directory is active: keep a kernel copy
    char executable_name[256];
    if (request.name == NULL || request.name_len == 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }
    memcpy(executable_name, request.name, request.name_len);
    executable_name[request.name_len] = '\0';
    request.name = executable_name;

    uint32_t executable_size = 0;
    if (get_file_size(request.name, request.name_len, request.parent_inode, &executable_size) != 0 ||
        executable_size == 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // Check whether memory is enough for the executable and additional frames for user stack
    uint32_t user_stack_bottom = PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_PAGE_COUNT * PAGE_FRAME_SIZE;
    if (executable_size > request.buffer_size ||
        executable_size > PROCESS_EXECUTABLE_SIZE_MAX ||
        entrypoint > user_stack_bottom ||
        executable_size > user_stack_bottom - entrypoint) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    uint32_t executable_page_count  = ceil_div(executable_size, PAGE_FRAME_SIZE);
    uint32_t page_frame_count_needed = executable_page_count + PROCESS_USER_STACK_PAGE_COUNT;
    // One more frame is needed for the page table of the new address space
    if (page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX ||
        !paging_allocate_check((page_frame_count_needed + 1) * PAGE_FRAME_SIZE)) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB
    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }
    struct ProcessControlBlock *new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    /* --- 1. Virtual address space --- */
    struct PageDirectory *page_directory = paging_create_new_page_directory();
    if (page_directory == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Page tables are reached through the recursive mapping, so the new
    // page directory has to be active while its frames are allocated
    struct PageDirectory *previous_page_directory = paging_get_current_page_directory_addr();
    paging_use_page_directory(page_directory);

    for (uint32_t i = 0; i < executable_page_count; i++) {
        if (!process_allocate_page(new_pcb, page_directory, (void *) (entrypoint + i * PAGE_FRAME_SIZE))) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto memory_cleanup;
        }
    }
    for (uint32_t i = 0; i < PROCESS_USER_STACK_PAGE_COUNT; i++) {
        if (!process_allocate_page(new_pcb, page_directory, (void *) (PROCESS_USER_STACK_TOP - (i + 1) * PAGE_FRAME_SIZE))) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto memory_cleanup;
        }
    }

    /* --- 2. Load executable into the new address space --- */
    request.buffer_size = executable_size;
    if (read(request) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto memory_cleanup;
    }

    /* --- 3. Initial state & context --- */
    process_setup_arguments(args);

    uint32_t user_data_selector = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.gs = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.stack.ebp  = PROCESS_USER_INITIAL_ESP;
    new_pcb->context.cpu.stack.esp  = PROCESS_USER_INITIAL_ESP;
    new_pcb->context.eip            = entrypoint;
    new_pcb->context.eflags        |= CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = page_directory;

    /* --- 4. Process metadata --- */
    struct ProcessControlBlock *parent_pcb = process_get_current_running_pcb_pointer();
    new_pcb->metadata.active      = true;
    new_pcb->metadata.pid         = process_generate_new_pid();
    new_pcb->metadata.parent_pid  = (parent_pcb != NULL) ? parent_pcb->metadata.pid : PROCESS_PID_NONE;
    new_pcb->metadata.waiting_pid = PROCESS_PID_NONE;
    new_pcb->metadata.state       = PROCESS_STATE_READY;
    uint32_t name_length = request.name_len;
    if (name_length > PROCESS_NAME_LENGTH_MAX - 1) name_length = PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(new_pcb->metadata.name, executable_name, name_length);
    new_pcb->metadata.name[name_length] = '\0';

    process_manager_state.active_process_count++;
    if (pid_out != NULL) *pid_out = new_pcb->metadata.pid;

    /* --- 5. Restore the address space used before process creation --- */
    paging_use_page_directory(previous_page_directory);
    goto exit_cleanup;

memory_cleanup:
    process_release_pages(new_pcb, page_directory);
    paging_use_page_directory(previous_page_directory);
    paging_free_page_directory(page_directory);
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

exit_cleanup:
    return retcode;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    return process_create_user_process_with_args(request, NULL, NULL);
}

int32_t process_create_init_process(void) {
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = PROCESS_INIT_NAME,
        .name_len     = sizeof(PROCESS_INIT_NAME) - 1,
        .parent_inode = PROCESS_INIT_PARENT_INODE,
        .buffer_size  = PROCESS_EXECUTABLE_SIZE_MAX,
        .is_directory = false,
    };
    uint32_t pid = PROCESS_PID_NONE;
    int32_t retcode = process_create_user_process_with_args(request, NULL, &pid);
    if (retcode == PROCESS_CREATE_SUCCESS) process_manager_state.init_pid = pid;
    return retcode;
}

uint32_t process_get_foreground_pid(void) {
    // A foreground process is one that another process is blocked on.
    // Skip the ones that are blocked themselves to reach the end of the chain.
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *waiter = &_process_list[i];
        if (!waiter->metadata.active || waiter->metadata.state != PROCESS_STATE_BLOCKED) continue;

        struct ProcessControlBlock *target = process_get_pcb_by_pid(waiter->metadata.waiting_pid);
        if (target != NULL && target->metadata.state != PROCESS_STATE_BLOCKED) {
            return target->metadata.pid;
        }
    }
    return PROCESS_PID_NONE;
}

/* --- Process list as text, used by the ps system call --- */

struct TextWriter {
    char     *buffer;
    uint32_t size;
    uint32_t length;
};

static void text_put(struct TextWriter *writer, char c) {
    // Always keep one byte for the null terminator
    if (writer->length + 1 < writer->size) writer->buffer[writer->length++] = c;
}

// Write text left aligned, padded with spaces up to width
static void text_put_string(struct TextWriter *writer, const char *text, uint32_t width) {
    uint32_t count = 0;
    while (text[count] != '\0') text_put(writer, text[count++]);
    while (count++ < width) text_put(writer, ' ');
}

static void text_put_number(struct TextWriter *writer, uint32_t value, uint32_t width) {
    char digits[11];
    uint32_t count = 0;
    do {
        digits[count++] = (char) ('0' + value % 10);
        value /= 10;
    } while (value > 0);

    char text[11];
    for (uint32_t i = 0; i < count; i++) text[i] = digits[count - 1 - i];
    text[count] = '\0';
    text_put_string(writer, text, width);
}

uint32_t process_get_list_text(char *buffer, uint32_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) return 0;

    struct TextWriter writer = { .buffer = buffer, .size = buffer_size, .length = 0 };
    text_put_string(&writer, "PID", 6);
    text_put_string(&writer, "PPID", 6);
    text_put_string(&writer, "STATE", 9);
    text_put_string(&writer, "NAME", 0);
    text_put(&writer, '\n');

    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (!pcb->metadata.active) continue;

        const char *state = "READY";
        if (pcb->metadata.state == PROCESS_STATE_RUNNING) state = "RUNNING";
        if (pcb->metadata.state == PROCESS_STATE_BLOCKED) state = "BLOCKED";

        text_put_number(&writer, pcb->metadata.pid, 6);
        text_put_number(&writer, pcb->metadata.parent_pid, 6);
        text_put_string(&writer, state, 9);
        text_put_string(&writer, pcb->metadata.name, 0);
        text_put(&writer, '\n');
        count++;
    }
    writer.buffer[writer.length] = '\0';
    return count;
}

bool process_destroy(uint32_t pid) {
    struct ProcessControlBlock *pcb = process_get_pcb_by_pid(pid);
    if (pcb == NULL) return false;

    struct PageDirectory *previous_page_directory = paging_get_current_page_directory_addr();
    struct PageDirectory *page_directory = pcb->context.page_directory_virtual_addr;

    // Free every user frame while the process address space is active,
    // then leave it before the page directory itself is released
    paging_use_page_directory(page_directory);
    process_release_pages(pcb, page_directory);
    paging_use_page_directory(&_paging_kernel_page_directory);
    paging_free_page_directory(page_directory);
    // When a process destroys itself there is nothing to go back to: stay on
    // the kernel page directory until the scheduler picks the next process
    if (previous_page_directory != page_directory) {
        paging_use_page_directory(previous_page_directory);
    }

    // Wake up every process that was waiting for this one
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *other = &_process_list[i];
        if (!other->metadata.active || other->metadata.waiting_pid != pid) continue;
        other->metadata.waiting_pid = PROCESS_PID_NONE;
        if (other->metadata.state == PROCESS_STATE_BLOCKED) {
            other->metadata.state = PROCESS_STATE_READY;
        }
    }

    memset(pcb, 0, sizeof(struct ProcessControlBlock));
    process_manager_state.active_process_count--;
    return true;
}
