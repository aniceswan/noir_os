#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/cmos.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include "header/user/syscall.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // +8 skips saved ebp and return address, so esp0 = caller esp before the call
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100);      // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010);      // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    // Setup how often PIT fire
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    // Activate the interrupt
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

/* -- Scheduler helpers -- */

static bool is_from_user_mode(const struct InterruptFrame *frame) {
    return (frame->int_stack.cs & 0x3) == 0x3;
}

/**
 * Build the context of the interrupted process out of the interrupt frame.
 * On inter-privilege interrupt, frame->cpu.stack.esp is the user esp (see intsetup.s).
 */
static struct Context context_from_frame(const struct InterruptFrame *frame) {
    struct Context ctx = {
        .cpu                         = frame->cpu,
        .eip                         = frame->int_stack.eip,
        .eflags                      = frame->int_stack.eflags,
        .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
    };
    return ctx;
}

/* -- Syscall helpers -- */

// The whole range [ptr, ptr + len) must lie below the kernel base
static bool is_user_range(const void *ptr, uint32_t len) {
    uint32_t start = (uint32_t) ptr;
    return ptr != NULL && start < KERNEL_VIRTUAL_ADDRESS_BASE && len <= KERNEL_VIRTUAL_ADDRESS_BASE - start;
}

static bool is_user_pointer(const void *ptr) {
    return is_user_range(ptr, sizeof(uint32_t));
}

static bool is_valid_fs_request(const struct EXT2DriverRequest *request, bool name_optional, bool need_buffer) {
    if (!is_user_range(request, sizeof(*request))) return false;
    if (request->parent_inode == 0) return false;
    if (request->name != NULL) {
        if (request->name_len == 0 || !is_user_range(request->name, request->name_len)) return false;
    } else if (!name_optional) {
        return false;
    }
    if (need_buffer && !is_user_range(request->buf, request->buffer_size)) return false;
    return true;
}

/**
 * Copy a null-terminated string out of user memory.
 *
 * @return false when the string leaves user memory or does not fit in cap bytes
 */
static bool copy_user_string(char *dst, const char *src, uint32_t cap) {
    for (uint32_t i = 0; i < cap; i++) {
        if (!is_user_range(src + i, 1)) return false;
        dst[i] = src[i];
        if (dst[i] == '\0') return true;
    }
    return false;
}

/**
 * Copy a NULL-terminated argv array out of user memory into kernel memory.
 * The caller's address space disappears while the new process is built, so
 * the arguments have to survive in the kernel in between.
 */
static bool copy_user_arguments(struct ProcessArguments *args, char **user_argv) {
    memset(args, 0, sizeof(struct ProcessArguments));
    if (user_argv == NULL) return true;

    uint32_t used = 0;
    for (uint32_t i = 0; i < PROCESS_ARG_COUNT_MAX; i++) {
        if (!is_user_range(&user_argv[i], sizeof(char *))) return false;
        const char *argument = user_argv[i];
        if (argument == NULL) break;

        if (!copy_user_string(args->buffer + used, argument, PROCESS_ARG_BUFFER_SIZE - used)) return false;
        args->offset[i] = used;
        while (args->buffer[used] != '\0') used++;
        used++;
        args->argc++;
    }
    return true;
}

/**
 * Walk an absolute path ("/bin/clock") from the root directory.
 *
 * @param path             Null-terminated path in kernel memory
 * @param parent_inode_out Inode of the directory containing the last component
 * @param name_out         Pointer (inside path) to the last component
 * @param name_len_out     Length of the last component
 * @return                 false when a directory on the way does not exist
 */
static bool resolve_path(const char *path, uint32_t *parent_inode_out, const char **name_out, uint8_t *name_len_out) {
    uint32_t inode = PROCESS_INIT_PARENT_INODE; // root directory
    const char *cursor = path;

    while (true) {
        while (*cursor == '/') cursor++;
        if (*cursor == '\0') return false;

        uint32_t length = 0;
        while (cursor[length] != '\0' && cursor[length] != '/') length++;
        if (length > 255) return false;

        const char *rest = cursor + length;
        while (*rest == '/') rest++;
        if (*rest == '\0') {
            *parent_inode_out = inode;
            *name_out         = cursor;
            *name_len_out     = (uint8_t) length;
            return true;
        }

        uint32_t child_inode = 0;
        uint8_t  file_type   = 0;
        if (get_entry_info(cursor, (uint8_t) length, inode, &child_inode, &file_type) != 0) return false;
        if (file_type != EXT2_FT_DIR) return false;
        inode  = child_inode;
        cursor = rest;
    }
}

/**
 * exec: create a new user process from an executable in the file system.
 * A foreground exec blocks the caller until the new process terminates.
 *
 * @return PID of the new process, -1 on failure
 */
static int32_t syscall_exec(struct InterruptFrame *frame) {
    // Static: too big for comfort on the stack, and syscalls are never reentrant
    static char path[256];
    static struct ProcessArguments arguments;

    bool is_background = frame->cpu.general.edx != 0;
    if (!copy_user_string(path, (const char *) frame->cpu.general.ebx, sizeof(path))) return -1;
    if (!copy_user_arguments(&arguments, (char **) frame->cpu.general.ecx)) return -1;

    uint32_t   parent_inode = 0;
    const char *name        = NULL;
    uint8_t    name_len     = 0;
    if (!resolve_path(path, &parent_inode, &name, &name_len)) return -1;

    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = (char *) name,
        .name_len     = name_len,
        .parent_inode = parent_inode,
        .buffer_size  = PROCESS_EXECUTABLE_SIZE_MAX,
        .is_directory = false,
    };

    uint32_t pid = PROCESS_PID_NONE;
    if (process_create_user_process_with_args(request, &arguments, &pid) != PROCESS_CREATE_SUCCESS) return -1;

    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    if (!is_background && current_pcb != NULL) {
        // Sleep until the child terminates. The saved context already carries
        // the return value, the caller resumes right after its "int 0x30".
        frame->cpu.general.eax = pid;
        scheduler_save_context_to_current_running_pcb(context_from_frame(frame));
        current_pcb->metadata.state       = PROCESS_STATE_BLOCKED;
        current_pcb->metadata.waiting_pid = pid;
        scheduler_switch_to_next_process();
    }
    return (int32_t) pid;
}

/**
 * System call dispatcher.
 *
 * | eax | service                | ebx                      | ecx            | edx        |
 * |-----|------------------------|--------------------------|----------------|------------|
 * | 0   | FS read()              | EXT2DriverRequest*       | int8_t* ret    | -          |
 * | 1   | FS read_directory()    | EXT2DriverRequest*       | int8_t* ret    | -          |
 * | 2   | FS write()             | EXT2DriverRequest*       | int8_t* ret    | -          |
 * | 3   | FS delete()            | EXT2DriverRequest*       | int8_t* ret    | -          |
 * | 4   | keyboard getchar()     | char* out                | -              | -          |
 * | 5   | putchar()              | char                     | text color     | -          |
 * | 6   | puts()                 | char* buffer             | char count     | text color |
 * | 7   | activate keyboard      | -                        | -              | -          |
 * | 8   | FS get_file_size()     | EXT2DriverRequest*       | uint32_t* out  | int8_t* ret|
 * | 9   | clear screen           | -                        | -              | -          |
 * | 10  | exit()                 | exit code                | -              | -          |
 * | 11  | exec()                 | char* absolute path      | char** argv    | background |
 * | 12  | kill()                 | pid                      | -              | -          |
 * | 13  | ps()                   | char* buffer             | buffer size    | -          |
 * | 14  | CMOS get time          | struct Time* out         | -              | -          |
 * | 15  | set text cursor        | row                      | col            | -          |
 * | 16  | get text cursor        | uint8_t* row             | uint8_t* col   | -          |
 * | 17  | getpid()               | -                        | -              | -          |
 * | 18  | write text at position | char* buffer             | char count     | position   |
 *
 * Service 18 packs its position into edx: row | col << 8 | color << 16.
 *
 * Return value in eax: exec -> new pid or -1, kill -> 1 when a process was
 * terminated, ps -> process count, getpid -> pid of the caller.
 */
void syscall(struct InterruptFrame *frame) {
    struct EXT2DriverRequest *request = (struct EXT2DriverRequest *) frame->cpu.general.ebx;
    uint32_t service = frame->cpu.general.eax;
    frame->cpu.general.eax = 0;

    switch (service) {
        case SYS_READ: {
            if (!is_valid_fs_request(request, false, true)) break;
            if (!is_user_pointer((void *) frame->cpu.general.ecx)) break;
            *((int8_t *) frame->cpu.general.ecx) = read(*request);
            break;
        }
        case SYS_READ_DIR: {
            if (!is_valid_fs_request(request, true, true)) break;
            if (!is_user_pointer((void *) frame->cpu.general.ecx)) break;
            *((int8_t *) frame->cpu.general.ecx) = read_directory(request);
            break;
        }
        case SYS_WRITE: {
            if (!is_valid_fs_request(request, false, false)) break;
            if (!request->is_directory && request->buffer_size > 0 &&
                !is_user_range(request->buf, request->buffer_size)) break;
            if (!is_user_pointer((void *) frame->cpu.general.ecx)) break;
            *((int8_t *) frame->cpu.general.ecx) = write(request);
            break;
        }
        case SYS_DELETE: {
            if (!is_valid_fs_request(request, false, false)) break;
            if (!is_user_pointer((void *) frame->cpu.general.ecx)) break;
            *((int8_t *) frame->cpu.general.ecx) = delete(*request);
            break;
        }
        case SYS_GETCHAR:
            if (is_user_pointer((void *) frame->cpu.general.ebx))
                get_keyboard_buffer((char *) frame->cpu.general.ebx);
            break;
        case SYS_PUTCHAR:
            framebuffer_putchar((char) frame->cpu.general.ebx, (uint8_t) frame->cpu.general.ecx);
            break;
        case SYS_PUTS:
            if (is_user_range((void *) frame->cpu.general.ebx, frame->cpu.general.ecx))
                framebuffer_puts(
                    (char *) frame->cpu.general.ebx,
                    frame->cpu.general.ecx,
                    (uint8_t) frame->cpu.general.edx
                );
            break;
        case SYS_ACTIVATE_KB:
            keyboard_state_activate();
            break;
        case SYS_FILE_SIZE: {
            if (!is_valid_fs_request(request, false, false)) break;
            if (!is_user_pointer((void *) frame->cpu.general.ecx)) break;
            if (!is_user_pointer((void *) frame->cpu.general.edx)) break;
            uint32_t size = 0;
            int8_t retcode = get_file_size(request->name, request->name_len, request->parent_inode, &size);
            if (retcode == 0) *((uint32_t *) frame->cpu.general.ecx) = size;
            *((int8_t *) frame->cpu.general.edx) = retcode;
            break;
        }
        case SYS_CLEAR_SCREEN:
            framebuffer_clear();
            framebuffer_set_cursor(0, 0);
            break;
        case SYS_SET_CURSOR:
            framebuffer_move_cursor((uint8_t) frame->cpu.general.ebx, (uint8_t) frame->cpu.general.ecx);
            break;
        case SYS_GET_CURSOR: {
            uint8_t *row_out = (uint8_t *) frame->cpu.general.ebx;
            uint8_t *col_out = (uint8_t *) frame->cpu.general.ecx;
            if (!is_user_pointer(row_out) || !is_user_pointer(col_out)) break;
            framebuffer_get_cursor(row_out, col_out);
            break;
        }
        case SYS_EXIT: {
            // Terminate the calling process, ebx (exit code of main) is not used.
            // The process is gone, so this syscall never returns to user mode.
            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            if (current_pcb != NULL) process_destroy(current_pcb->metadata.pid);
            scheduler_switch_to_next_process();
        }
        case SYS_EXEC:
            frame->cpu.general.eax = (uint32_t) syscall_exec(frame);
            break;
        case SYS_KILL: {
            uint32_t pid = frame->cpu.general.ebx;
            if (process_get_pcb_by_pid(pid) == NULL) break;

            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            bool is_self = current_pcb != NULL && current_pcb->metadata.pid == pid;
            process_destroy(pid);
            // A process that killed itself has nowhere to return to
            if (is_self) scheduler_switch_to_next_process();
            frame->cpu.general.eax = 1;
            break;
        }
        case SYS_PS:
            if (is_user_range((void *) frame->cpu.general.ebx, frame->cpu.general.ecx))
                frame->cpu.general.eax = process_get_list_text(
                    (char *) frame->cpu.general.ebx,
                    frame->cpu.general.ecx
                );
            break;
        case SYS_GETPID: {
            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            if (current_pcb != NULL) frame->cpu.general.eax = current_pcb->metadata.pid;
            break;
        }
        case SYS_GET_TIME: {
            struct Time *time_out = (struct Time *) frame->cpu.general.ebx;
            if (!is_user_range(time_out, sizeof(struct Time))) break;
            cmos_read_time(time_out);
            break;
        }
        case SYS_WRITE_AT: {
            // Write text at a fixed screen position, the text cursor does not move
            const char *text  = (const char *) frame->cpu.general.ebx;
            uint32_t   count  = frame->cpu.general.ecx;
            uint32_t   row    = frame->cpu.general.edx & 0xFF;
            uint32_t   col    = (frame->cpu.general.edx >> 8) & 0xFF;
            uint8_t    color  = (uint8_t) ((frame->cpu.general.edx >> 16) & 0xFF);
            if (!is_user_range(text, count) || row >= FRAMEBUFFER_HEIGHT) break;
            for (uint32_t i = 0; i < count && col + i < FRAMEBUFFER_WIDTH; i++) {
                framebuffer_write((uint8_t) row, (uint8_t) (col + i), text[i], color & 0x0F, color >> 4);
            }
            break;
        }
        default:
            // Unknown service
            frame->cpu.general.eax = (uint32_t) -1;
            break;
    }
}

/* -- Exception helpers -- */

static void fb_print(const char *s) {
    uint32_t count = 0;
    while (s[count] != '\0') count++;
    framebuffer_puts(s, count, 0x0C);
}

static void fb_print_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[8];
    for (int32_t i = 7; i >= 0; i--) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }
    framebuffer_puts(buffer, 8, 0x0C);
}

__attribute__((noreturn)) static void halt_with_fault(const char *name, struct InterruptFrame *frame) {
    framebuffer_putchar('\n', 0x0C);
    fb_print(name);
    fb_print(" error_code=0x");
    fb_print_hex(frame->int_stack.error_code);
    fb_print(" eip=0x");
    fb_print_hex(frame->int_stack.eip);
    framebuffer_putchar('\n', 0x0C);
    while (true) __asm__ volatile("hlt");
}

static void fb_print_decimal(uint32_t value) {
    char buffer[10];
    uint32_t digits = 0;
    do {
        buffer[digits++] = (char) ('0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (digits > 0) framebuffer_putchar(buffer[--digits], 0x0C);
}

/**
 * Handle a CPU exception. A fault raised by a user process only terminates
 * that process, the rest of the system keeps running. A fault raised by the
 * kernel itself is fatal.
 *
 * @param on_behalf_of_process The kernel faulted while serving the running
 *                             process, e.g. a system call got a bad pointer
 */
static void handle_cpu_exception(const char *name, struct InterruptFrame *frame, bool on_behalf_of_process) {
    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    bool is_process_fault = is_from_user_mode(frame) || on_behalf_of_process;
    if (!is_process_fault || current_pcb == NULL) halt_with_fault(name, frame);

    framebuffer_putchar('\n', 0x0C);
    fb_print(current_pcb->metadata.name);
    fb_print(" (pid ");
    fb_print_decimal(current_pcb->metadata.pid);
    fb_print(") terminated: ");
    fb_print(name);
    fb_print(" eip=0x");
    fb_print_hex(frame->int_stack.eip);
    framebuffer_putchar('\n', 0x0C);

    process_destroy(current_pcb->metadata.pid);
    scheduler_switch_to_next_process();
}

/**
 * Ctrl+C: terminate the foreground process, which wakes up the process
 * (the shell) that was waiting for it.
 */
static void interrupt_foreground_process(void) {
    uint32_t pid = process_get_foreground_pid();
    if (pid == PROCESS_PID_NONE) return;

    struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
    bool is_current = current_pcb != NULL && current_pcb->metadata.pid == pid;

    framebuffer_puts("^C\n", 3, 0x0F);
    process_destroy(pid);
    // The interrupted process itself is gone, there is nothing to return to
    if (is_current) scheduler_switch_to_next_process();
}

void main_interrupt_handler(struct InterruptFrame *frame) {
    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            // ACK first: the context switch below never comes back here
            pic_ack(IRQ_TIMER);
            if (is_from_user_mode(frame)) {
                scheduler_save_context_to_current_running_pcb(context_from_frame(frame));
                scheduler_switch_to_next_process();
            }
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            if (keyboard_consume_interrupt_request()) interrupt_foreground_process();
            break;
        case 0x0D:
            handle_cpu_exception("GENERAL PROTECTION FAULT", frame, false);
            break;
        case 0x0E: {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2) : /* <Empty> */);
            framebuffer_putchar('\n', 0x0C);
            fb_print("PAGE FAULT at 0x");
            fb_print_hex(cr2);
            // A kernel-mode fault on a user address comes from a system call
            // that was handed an unmapped pointer: blame the calling process
            handle_cpu_exception("PAGE FAULT", frame, cr2 < KERNEL_VIRTUAL_ADDRESS_BASE);
            break;
        }
        case 0x30:
            syscall(frame);
            break;
        default:
            // Any other CPU exception: never return with iret to the faulting instruction
            if (frame->int_number < 32) handle_cpu_exception("CPU EXCEPTION", frame, false);
            break;
    }
}
