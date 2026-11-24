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

static void print(const char *s) {
    uint32_t count = 0;
    while (s[count] != '\0') count++;
    framebuffer_puts(s, count, 0x0F);
}

void kernel_setup(void) {
    // Chapter 0: Global Descriptor Table
    load_gdt(&_gdt_gdtr);

    // Chapter 1: Interrupts & Hardware Drivers
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();

    // Chapter 1: Clear screen (higher-half MMIO 0xC00B8000)
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    // Chapter 2: EXT2 File System Initializer
    initialize_filesystem_ext2();

    // Chapter 3: Task State Segment (TSS) for Ring 0 <-> Ring 3 transitions
    gdt_install_tss();
    set_tss_register();

    // Set TSS.esp0 for interprivilege interrupt
    set_tss_kernel_current_stack();

    // Chapter 4: the shell is the init process, loaded from the EXT2 root directory
    int32_t retcode = process_create_init_process();
    if (retcode != PROCESS_CREATE_SUCCESS) {
        if (retcode == PROCESS_CREATE_FAIL_FS_READ_FAILURE)
            print("Error: Failed to load shell binary from disk! (run 'make prepare')\n");
        else
            print("Error: Failed to create the init process!\n");
        while (true) __asm__ volatile("hlt");
    }

    // Chapter 4: start the preemptive scheduler and jump into the init process
    scheduler_init();
    scheduler_switch_to_next_process();
}
