#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/cmos.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/text/framebuffer.h"
#include "header/user/syscall.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for the local variable
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

/* -- Syscall helpers -- */

static bool is_user_pointer(const void *ptr) {
    return ptr != NULL && (uint32_t) ptr < KERNEL_VIRTUAL_ADDRESS_BASE;
}

static bool is_valid_fs_request(const struct EXT2DriverRequest *request, bool name_optional, bool need_buffer) {
    if (!is_user_pointer(request)) return false;
    if (request->parent_inode == 0) return false;
    if (request->name != NULL) {
        if (!is_user_pointer(request->name) || request->name_len == 0) return false;
    } else if (!name_optional) {
        return false;
    }
    if (need_buffer && !is_user_pointer(request->buf)) return false;
    return true;
}

static uint32_t user_string_length(const char *str, uint32_t max_length) {
    for (uint32_t i = 0; i < max_length; i++) {
        if (str[i] == '\0') return i;
    }
    return max_length + 1;
}

/**
 * Resolve an absolute path ("/bin/clock") to its parent directory inode and
 * final component name, used by the exec system call.
 */
static int8_t resolve_exec_path(
    const char *path,
    uint32_t *parent_inode_out,
    const char **name_out,
    uint8_t *name_len_out
) {
    if (path == NULL || path[0] != '/') return -1;

    uint32_t inode = 1; // root inode in EXT2 IF2130 Edition
    const char *cursor = path + 1;
    if (*cursor == '\0') return -1;

    while (true) {
        const char *slash = cursor;
        while (*slash != '\0' && *slash != '/') slash++;
        uint32_t segment_length = (uint32_t) (slash - cursor);
        if (segment_length == 0 || segment_length > 255) return -1;

        if (*slash == '\0') {
            *parent_inode_out = inode;
            *name_out = cursor;
            *name_len_out = (uint8_t) segment_length;
            return 0;
        }

        uint32_t child_inode = 0;
        uint8_t child_type = 0;
        if (get_entry_info(cursor, (uint8_t) segment_length, inode, &child_inode, &child_type) != 0) {
            return -2;
        }
        if (child_type != EXT2_FT_DIR) return -2;

        inode = child_inode;
        cursor = slash + 1;
        if (*cursor == '\0') return -1;
    }
}

static uint32_t append_string(char *buffer, uint32_t capacity, uint32_t length, const char *str) {
    while (*str != '\0' && length < capacity) buffer[length++] = *str++;
    return length;
}

static uint32_t append_uint(char *buffer, uint32_t capacity, uint32_t length, uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value > 0) {
        digits[count++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && length < capacity) buffer[length++] = digits[--count];
    return length;
}

static const char *process_state_name(PROCESS_STATE state) {
    switch (state) {
        case PROCESS_STATE_READY:   return "READY";
        case PROCESS_STATE_RUNNING: return "RUNNING";
        case PROCESS_STATE_TERMINATED: return "TERMINATED";
        default: return "UNUSED";
    }
}

/**
 * System call dispatcher. Called from main_interrupt_handler() when the
 * user program executes `int 0x30`.
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
 * | 10  | exit                   | exit code                | -              | -          |
 * | 11  | exec                   | char* path               | char** argv    | flags      |
 * | 12  | kill                   | pid                      | -              | -          |
 * | 13  | ps                     | char* buffer             | capacity       | -          |
 * | 14  | get RTC time           | struct RTCTime*          | -              | -          |
 * | 15  | set text cursor        | row                      | col            | -          |
 * | 16  | get text cursor        | uint8_t* row             | uint8_t* col   | -          |
 * | 17  | getpid                 | -                        | -              | -          |
 * | 18  | write at (row,col)     | (row<<8)|col             | char* buffer   | (color<<8)|len |
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
            if (!request->is_directory && request->buffer_size > 0 && !is_user_pointer(request->buf)) break;
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
            if (is_user_pointer((void *) frame->cpu.general.ebx))
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
        case SYS_EXIT: {
            struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
            uint32_t exit_code = frame->cpu.general.ebx;
            (void) exit_code;
            if (pcb == NULL) break;
            process_destroy(pcb->metadata.pid);
            scheduler_switch_to_next_process();
            break;
        }
        case SYS_EXEC: {
            const char *path = (const char *) frame->cpu.general.ebx;
            const char **user_argv = (const char **) frame->cpu.general.ecx;
            uint32_t flags = frame->cpu.general.edx;
            (void) flags;

            if (!is_user_pointer(path) || user_string_length(path, 255) > 255) {
                frame->cpu.general.eax = (uint32_t) -1;
                break;
            }

            uint32_t parent_inode = 0;
            const char *file_name = NULL;
            uint8_t file_name_len = 0;
            if (resolve_exec_path(path, &parent_inode, &file_name, &file_name_len) != 0) {
                frame->cpu.general.eax = (uint32_t) -2;
                break;
            }

            const char *argv[8];
            int argc = 0;
            if (is_user_pointer(user_argv)) {
                while (argc < 8) {
                    const char *arg = user_argv[argc];
                    if (!is_user_pointer(arg) || user_string_length(arg, 255) > 255) break;
                    argv[argc] = arg;
                    argc++;
                }
            }
            if (argc == 0) {
                argv[0] = file_name;
                argc = 1;
            }

            struct EXT2DriverRequest exec_request = {
                .buf          = (void *) 0,
                .name         = (char *) file_name,
                .name_len     = file_name_len,
                .parent_inode = parent_inode,
                .buffer_size  = 0,
                .is_directory = false,
            };
            int32_t status = process_create_user_process_args(exec_request, argc, argv);
            if (status != PROCESS_CREATE_SUCCESS) {
                frame->cpu.general.eax = (uint32_t) (-10 - status);
                break;
            }
            /* process_create_user_process_args() just generated a PID. */
            frame->cpu.general.eax = process_manager_state.next_pid - 1;
            break;
        }
        case SYS_KILL: {
            uint32_t pid = frame->cpu.general.ebx;
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
            bool is_current = (current != NULL && current->metadata.pid == pid);
            frame->cpu.general.eax = process_destroy(pid) ? 1 : 0;
            if (is_current) {
                scheduler_switch_to_next_process();
            }
            break;
        }
        case SYS_PS: {
            char *buffer = (char *) frame->cpu.general.ebx;
            uint32_t capacity = frame->cpu.general.ecx;
            if (!is_user_pointer(buffer) || capacity == 0) {
                frame->cpu.general.eax = (uint32_t) -1;
                break;
            }
            uint32_t length = 0;
            uint32_t count = 0;
            for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
                struct ProcessControlBlock *pcb = &_process_list[i];
                if (pcb->metadata.state == PROCESS_STATE_UNUSED) continue;
                length = append_string(buffer, capacity, length, "PID ");
                length = append_uint(buffer, capacity, length, pcb->metadata.pid);
                length = append_string(buffer, capacity, length, "  ");
                length = append_string(buffer, capacity, length, pcb->metadata.name);
                length = append_string(buffer, capacity, length, "  ");
                length = append_string(buffer, capacity, length, process_state_name(pcb->metadata.state));
                length = append_string(buffer, capacity, length, "\n");
                count++;
            }
            if (length < capacity) buffer[length] = '\0';
            frame->cpu.general.eax = count;
            break;
        }
        case SYS_GET_TIME: {
            struct RTCTime *time_out = (struct RTCTime *) frame->cpu.general.ebx;
            if (!is_user_pointer(time_out)) break;
            cmos_read_rtc(time_out);
            break;
        }
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
        case SYS_GETPID: {
            struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
            frame->cpu.general.eax = (pcb != NULL) ? pcb->metadata.pid : 0;
            break;
        }
        case SYS_WRITE_AT: {
            uint32_t row = (frame->cpu.general.ebx >> 8) & 0xFF;
            uint32_t col = frame->cpu.general.ebx & 0xFF;
            const char *buffer = (const char *) frame->cpu.general.ecx;
            uint32_t length = frame->cpu.general.edx & 0xFF;
            uint8_t color = (uint8_t) ((frame->cpu.general.edx >> 8) & 0xFF);
            if (!is_user_pointer(buffer)) break;
            if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH) break;
            for (uint32_t i = 0; i < length && col + i < FRAMEBUFFER_WIDTH; i++) {
                framebuffer_write((uint8_t) row, (uint8_t) (col + i), buffer[i],
                                  color & 0x0F, (color >> 4) & 0x0F);
            }
            break;
        }
        default:
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

/**
 * CPU exception reached from user mode: terminate the offending process and
 * keep the system alive instead of halting the whole OS.
 */
__attribute__((noreturn)) static void user_fault_exit(const char *name, struct InterruptFrame *frame) {
    framebuffer_putchar('\n', 0x0C);
    fb_print("user ");
    fb_print(name);
    fb_print(" eip=0x");
    fb_print_hex(frame->int_stack.eip);
    framebuffer_putchar('\n', 0x0C);

    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb != NULL) {
        process_destroy(pcb->metadata.pid);
        scheduler_switch_to_next_process();
    }
    halt_with_fault(name, frame);
}

void main_interrupt_handler(struct InterruptFrame *frame) {
    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER: {
            pic_ack(IRQ_TIMER);
            /* While idling there is no process to save; return normally so
             * the ISR stub iret's back into the idle hlt loop instead of
             * growing the kernel stack by recursing into the scheduler. */
            if (process_get_current_running_pcb_pointer() == NULL
                && !scheduler_has_runnable_process()) {
                break;
            }
            struct Context context = {
                .cpu = frame->cpu,
                .eip = frame->int_stack.eip,
                .eflags = frame->int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
            };
            scheduler_save_context_to_current_running_pcb(context);
            scheduler_switch_to_next_process();
            break;
        }
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 0x0D:
            if ((frame->int_stack.cs & 0x3) == 0x3) {
                user_fault_exit("GENERAL PROTECTION FAULT", frame);
            }
            halt_with_fault("GENERAL PROTECTION FAULT", frame);
        case 0x0E: {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2) : /* <Empty> */);
            framebuffer_putchar('\n', 0x0C);
            fb_print("PAGE FAULT at 0x");
            fb_print_hex(cr2);
            if ((frame->int_stack.cs & 0x3) == 0x3) {
                user_fault_exit("PAGE FAULT", frame);
            }
            halt_with_fault("", frame);
        }
        case 0x30:
            syscall(frame);
            break;
        default:
            break;
    }
}
