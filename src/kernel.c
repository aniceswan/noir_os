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

#define SHELL_PARENT_INODE 1u          // root inode in EXT2 IF2130 Edition
#define SHELL_MAX_SIZE     0x100000u   // 1 MiB

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

    print("mbOSok M3: loading shell into user mode (Ring 3)...\n");

    // 1. Allocate 4 page frames for user stack (0x3FC000 - 0x3FFFFF)
    for (uint32_t i = 0; i < 4; i++) {
        paging_allocate_user_page_frame(
            &_paging_kernel_page_directory,
            (void *) (0x3FC000 + i * PAGE_FRAME_SIZE)
        );
    }

    // 2. Query shell binary size from EXT2 disk
    uint32_t file_size = 0;
    if (get_file_size("shell", 5, SHELL_PARENT_INODE, &file_size) != 0 || file_size == 0) {
        print("Error: Failed to find shell binary on disk! (run 'make prepare')\n");
        while (true) __asm__ volatile("hlt");
    }

    // 3. Allocate page frames for shell executable image starting at virtual 0x0
    uint32_t code_frame_count = (file_size + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    for (uint32_t i = 0; i < code_frame_count; i++) {
        paging_allocate_user_page_frame(
            &_paging_kernel_page_directory,
            (void *) (i * PAGE_FRAME_SIZE)
        );
    }

    // 4. Load shell binary directly into virtual address 0x0
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = SHELL_PARENT_INODE,
        .buffer_size  = file_size,
        .is_directory = false,
    };
    if (read(request) != 0) {
        print("Error: Failed to read shell binary into memory!\n");
        while (true) __asm__ volatile("hlt");
    }

    // 5. Setup TSS esp0 kernel stack pointer for inter-privilege syscalls/interrupts
    set_tss_kernel_current_stack();

    print("Transitioning to User Mode...\n");

    // 6. Execute shell in user mode (Ring 3) via iret technique
    kernel_execute_user_program((void *) 0x0);

    while (true) __asm__ volatile("hlt");
}
