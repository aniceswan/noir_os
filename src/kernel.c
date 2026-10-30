#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/stdlib/string.h"

#define SHELL_PARENT_INODE 1u          // root inode in EXT2 IF2130 Edition
#define SHELL_MAX_SIZE     0x100000u   // 1 MiB

static void print(const char *s) {
    uint32_t count = 0;
    while (s[count] != '\0') count++;
    framebuffer_puts(s, count, 0x0F);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);

    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    initialize_filesystem_ext2();

    gdt_install_tss();
    set_tss_register();

    print("mbOSok M4: creating init process (shell)...\n");

    struct EXT2DriverRequest request = {
        .buf          = (uint8_t *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = SHELL_PARENT_INODE,
        .buffer_size  = SHELL_MAX_SIZE,
        .is_directory = false,
    };

    // Set TSS.esp0 for inter-privilege interrupts
    set_tss_kernel_current_stack();

    int32_t retcode = process_create_user_process(request);
    if (retcode != PROCESS_CREATE_SUCCESS) {
        print("Failed to create init process, error code ");
        framebuffer_putchar((char) ('0' + retcode), 0x0C);
        print(" (run 'make prepare')\n");
        while (true) __asm__ volatile("hlt");
    }

    scheduler_init();
    scheduler_switch_to_next_process();

    while (true) __asm__ volatile("hlt");
}
