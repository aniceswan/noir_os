global process_context_switch

; Offsets inside struct Context (see header/process/process.h)
CTX_EDI    equ 0
CTX_ESI    equ 4
CTX_EBP    equ 8
CTX_ESP    equ 12
CTX_EBX    equ 16
CTX_EDX    equ 20
CTX_ECX    equ 24
CTX_EAX    equ 28
CTX_GS     equ 32
CTX_FS     equ 36
CTX_ES     equ 40
CTX_DS     equ 44
CTX_EIP    equ 48
CTX_EFLAGS equ 52

USER_CODE_SELECTOR equ 0x18 | 0x3   ; GDT_USER_CODE_SEGMENT_SELECTOR, ring 3
USER_DATA_SELECTOR equ 0x20 | 0x3   ; GDT_USER_DATA_SEGMENT_SELECTOR, ring 3

section .text
; void process_context_switch(struct Context ctx);
; Load CPU register from ctx and jump into the process with iret, never returns.
; The page directory (CR3) is already switched by the scheduler in C.
process_context_switch:
    ; 1. ctx is passed by value: [esp] is the return address, ctx starts at [esp+4]
    lea  ecx, [esp+4]

    ; 2. Setup iret stack for inter-privilege return (ring 0 -> ring 3)
    ;    [esp] -> eip, [esp+4] -> cs, [esp+8] -> eflags, [esp+12] -> user esp, [esp+16] -> user ss
    mov  eax, USER_DATA_SELECTOR
    push eax                        ; ss
    mov  eax, [ecx + CTX_ESP]
    push eax                        ; esp
    mov  eax, [ecx + CTX_EFLAGS]
    push eax                        ; eflags
    mov  eax, USER_CODE_SELECTOR
    push eax                        ; cs
    mov  eax, [ecx + CTX_EIP]
    push eax                        ; eip

    ; 3. Load all register from ctx, segment register borrows eax as temporary
    mov  eax, [ecx + CTX_GS]
    mov  gs, ax
    mov  eax, [ecx + CTX_FS]
    mov  fs, ax
    mov  eax, [ecx + CTX_ES]
    mov  es, ax
    mov  eax, [ecx + CTX_DS]
    mov  ds, ax                     ; Flat segment, ctx is still reachable through ds

    mov  edi, [ecx + CTX_EDI]
    mov  esi, [ecx + CTX_ESI]
    mov  ebp, [ecx + CTX_EBP]
    mov  ebx, [ecx + CTX_EBX]
    mov  edx, [ecx + CTX_EDX]

    ; 4. eax and ecx are restored last because they were used as temporary & ctx base
    mov  eax, [ecx + CTX_EAX]
    mov  ecx, [ecx + CTX_ECX]

    ; 5. Jump to the process
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
