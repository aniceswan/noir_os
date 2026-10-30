#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
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

/**
 * System call dispatcher for Milestone 3 (User Mode & Shell).
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
 * | 15  | set text cursor        | row                      | col            | -          |
 * | 16  | get text cursor        | uint8_t* row             | uint8_t* col   | -          |
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

void main_interrupt_handler(struct InterruptFrame *frame) {
    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 0x0D:
            halt_with_fault("GENERAL PROTECTION FAULT", frame);
        case 0x0E: {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2) : /* <Empty> */);
            framebuffer_putchar('\n', 0x0C);
            fb_print("PAGE FAULT at 0x");
            fb_print_hex(cr2);
            halt_with_fault("", frame);
        }
        case 0x30:
            syscall(frame);
            break;
        default:
            break;
    }
}
