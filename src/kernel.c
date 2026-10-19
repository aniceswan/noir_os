#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"

static void kernel_puts(uint8_t row, uint8_t col, const char *str, uint8_t fg, uint8_t bg) {
    uint8_t c_col = col;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            row++;
            c_col = col;
        } else {
            framebuffer_write(row, c_col, str[i], fg, bg);
            c_col++;
            if (c_col >= FRAMEBUFFER_WIDTH) {
                c_col = col;
                row++;
            }
        }
    }
}

void kernel_setup(void) {
    // Chapter 0: Load Global Descriptor Table
    load_gdt(&_gdt_gdtr);

    // Chapter 1: Interrupt setup
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    // Chapter 1: Clear framebuffer
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    kernel_puts(1, 2, "=== mbOSok Operating System (IF2130) ===", 0xA, 0x0);
    kernel_puts(2, 2, "Chapter 0 & 1: GDT, IDT, PIC, Framebuffer, Keyboard OK", 0x7, 0x0);

    // Chapter 2.2: Disk Driver test (PDF page 71)
    struct BlockBuffer b;
    for (int i = 0; i < 512; i++) b.buf[i] = i % 16;
    write_blocks(&b, 17, 1);
    kernel_puts(4, 2, "[Disk Driver] Write 512 bytes to block 17: OK", 0xB, 0x0);

    // Chapter 2.5 & 2.6: File System EXT2 Initializer & CRUD test
    initialize_filesystem_ext2();
    kernel_puts(5, 2, "[EXT2 FS] Filesystem initialized: OK", 0xB, 0x0);

    // Test writing a file to root directory
    char file_content[] = "Hello from mbOSok EXT2 File System!";
    struct EXT2DriverRequest write_req = {
        .buf = file_content,
        .name = "hello.txt",
        .name_len = 9,
        .parent_inode = 1,
        .buffer_size = sizeof(file_content) - 1,
        .is_directory = false,
    };
    int8_t w_ret = write(&write_req);

    // Test reading back the file
    char read_buf[64];
    for (int i = 0; i < 64; i++) read_buf[i] = 0;
    struct EXT2DriverRequest read_req = {
        .buf = read_buf,
        .name = "hello.txt",
        .name_len = 9,
        .parent_inode = 1,
        .buffer_size = sizeof(read_buf),
        .is_directory = false,
    };
    int8_t r_ret = read(read_req);

    if ((w_ret == 0 || w_ret == 1) && r_ret == 0) {
        kernel_puts(6, 2, "[EXT2 FS] Write & Read 'hello.txt': SUCCESS", 0x2, 0x0);
        kernel_puts(7, 4, read_buf, 0xE, 0x0);
    } else {
        kernel_puts(6, 2, "[EXT2 FS] Write & Read: FAILED", 0xC, 0x0);
    }

    kernel_puts(9, 2, "Type anything on your keyboard below:", 0xF, 0x0);
    framebuffer_set_cursor(11, 2);

    // Interactive keyboard typing
    int row = 11, col = 2;
    keyboard_state_activate();
    while (true) {
        char c = 0;
        get_keyboard_buffer(&c);
        if (c) {
            if (c == '\n') {
                row++;
                col = 2;
            } else if (c == '\b') {
                if (col > 2) {
                    col--;
                    framebuffer_write(row, col, ' ', 0x7, 0x0);
                }
            } else {
                framebuffer_write(row, col, c, 0xF, 0);
                col++;
                if (col >= FRAMEBUFFER_WIDTH - 2) {
                    col = 2;
                    row++;
                }
            }
            framebuffer_set_cursor(row, col);
        }
    }
}
