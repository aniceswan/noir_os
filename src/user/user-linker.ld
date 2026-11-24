ENTRY(_start)

SECTIONS {
    . = 0x00000000; /* Assuming OS will load this program at virtual address */

    .text ALIGN(4): {
        *crt0.o(.text)  /* Put the entrypoint in the front of executable */
        *(.text)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }

    .data ALIGN(4): {
        *(.data)
    }

    .bss ALIGN(4): { /* Zeroed by the kernel before the program is loaded */
        *(COMMON)
        *(.bss)
    }
    /* Notes and unwind tables would end up in the flat binary, possibly before _start */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
    ASSERT ((_start == 0), "Error: _start must be at address 0, kernel jumps there")
}
