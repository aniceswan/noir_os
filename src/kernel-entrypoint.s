global loader                        ; the entry symbol for ELF
global load_gdt                      ; load GDT table
global set_tss_register              ; set tss register to GDT entry
global kernel_execute_user_program   ; execute initial user program from kernel
extern kernel_setup                  ; kernel C entrypoint
extern _paging_kernel_page_directory ; kernel page directory
extern _paging_kernel_page_table     ; kernel page table

KERNEL_VIRTUAL_BASE equ 0xC0000000    ; kernel virtual memory
KERNEL_STACK_SIZE   equ 2097152       ; size of stack in bytes (2 MiB)
MAGIC_NUMBER        equ 0x1BADB002    ; define the magic number constant
FLAGS               equ 0x0           ; multiboot flags
CHECKSUM            equ -MAGIC_NUMBER ; calculate the checksum (magic number + checksum + flags == 0)
; Initial user esp, must match PROCESS_USER_INITIAL_ESP in process.h:
; stack top 0x400000 - argument strings (256) - argv[9] (36) - argv & argc (8)
USER_INITIAL_ESP    equ 0x400000 - 256 - 36 - 8


section .bss
align 4                    ; align at 4 bytes
kernel_stack:              ; label points to beginning of memory
    resb KERNEL_STACK_SIZE ; reserve stack for the kernel


section .multiboot  ; GRUB multiboot header
align 4             ; the code must be 4 byte aligned
    dd MAGIC_NUMBER ; write the magic number to the machine code,
    dd FLAGS        ; the flags,
    dd CHECKSUM     ; and the checksum


; start of the text (code) section
section .setup.text
loader equ (loader_entrypoint - KERNEL_VIRTUAL_BASE)
loader_entrypoint:         ; the loader label (defined as entry point in linker script)
; Map the first 4 MiB (4 KiB pages) both at 0x0 and at 0xC0000000 before paging is on.
    mov edi, (_paging_kernel_page_table - KERNEL_VIRTUAL_BASE)
    xor eax, eax
    mov ecx, 1024
.fill_kernel_page_table:
    mov edx, eax
    or  edx, 0x3
    mov [edi], edx
    add edi, 4
    add eax, 0x1000
    loop .fill_kernel_page_table

    mov eax, (_paging_kernel_page_table - KERNEL_VIRTUAL_BASE)
    or  eax, 0x3
    ; Identity map (removed after the jump) and higher-half map at PDE[0x300]
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE)], eax
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xC00)], eax

    ; Recursive (fractal) mapping: PDE[1023] points to the page directory itself.
    ; Required by paging_get_page_table(); 1023 * 4 = 0xFFC.
    mov eax, (_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE)
    or  eax, 0x3
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xFFC)], eax

    ; Set CR3 (CPU page register)
    mov eax, (_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, eax

    ; Disable 4 MiB pages; all mappings use 4 KiB page tables.
    mov eax, cr4
    and eax, 0xFFFFFFEF
    mov cr4, eax

    ; Enable paging
    mov eax, cr0
    or  eax, 0x80000000    ; PG flag
    mov cr0, eax

    ; Jump into higher half first, cannot use C because call stack is still not working
    lea eax, [loader_virtual]
    jmp eax

loader_virtual:
    mov dword [(_paging_kernel_page_directory)], 0
    mov ecx, cr3                              ; Identity mapping deleted, reload cr3 to flush the whole TLB
    mov cr3, ecx
    mov esp, kernel_stack + KERNEL_STACK_SIZE ; Setup stack register to proper location
    call kernel_setup
.loop:
    hlt
    jmp .loop                                 ; loop forever


section .text
; More details: https://en.wikibooks.org/wiki/X86_Assembly/Protected_Mode
load_gdt:
    cli
    mov  eax, [esp+4]
    lgdt [eax] ; Load GDT from GDTDescriptor, eax at this line will point GDTR location

    ; Set bit-0 (Protection Enable bit-flag) in Control Register 0 (CR0)
    ; This is optional, as usually GRUB already start with protected mode flag enabled
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    ; Far jump to update cs register
    ; Warning: Invalid GDT will raise exception in any instruction below
    jmp 0x8:flush_cs
flush_cs:
    ; Update all segment register
    mov ax, 10h
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret


set_tss_register:
    mov ax, 0x28 | 0 ; GDT TSS Selector, ring 0
    ltr ax
    ret


kernel_execute_user_program:
    mov  eax, 0x20 | 0x3           ; GDT_USER_DATA_SEGMENT_SELECTOR | ring 3
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Using iret (return instruction for interrupt) technique for privilege change
    ; Stack values will be loaded into these register:
    ; [esp] -> eip, [esp+4] -> cs, [esp+8] -> eflags, [esp+12] -> user esp, [esp+16] -> user ss
    mov  ecx, [esp+4]              ; Save the program entry point before pushing anything
    push eax                       ; Stack segment selector (user data, ring 3)
    mov  eax, USER_INITIAL_ESP     ; User space stack pointer, same convention as process creation
    push eax                       ; User esp
    pushf                          ; eflags register state when entering user program
    mov  eax, 0x18 | 0x3           ; GDT_USER_CODE_SEGMENT_SELECTOR | ring 3
    push eax
    mov  eax, ecx
    push eax                       ; eip register to jump to

    iret

section .note.GNU-stack noalloc noexec nowrite progbits
