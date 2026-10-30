global _start
extern main

; Process entrypoint.
; On entry the kernel-provided user stack follows the cdecl main() layout:
;   [esp]            = argc
;   [esp+4]          = pointer to argv[0]
;   [esp+8]          = argv[0]
;   [esp+12]         = argv[1]
;   ...
section .text
_start:
    mov ebp, esp
    call main

    ; Terminate the process after main() returns (exit syscall = 10)
    mov ebx, eax
    mov eax, 10
    int 0x30

    ; Should never be reached
    cli
    hlt

section .note.GNU-stack noalloc noexec nowrite progbits
