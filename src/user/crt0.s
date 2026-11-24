global _start
extern main

; Process entrypoint, linked at the very beginning of every user program.
; Stack prepared by the kernel on process creation:
;   [esp + 4] char **argv
;   [esp + 0] int argc
; After "call main" pushes the return address, main() finds argc at [esp + 4]
; and argv at [esp + 8], exactly as the cdecl calling convention expects.
section .text
_start:
    mov ebp, esp
    call main

    ; main() returned: ask the kernel to terminate this process
    mov ebx, eax    ; Exit code, return value of main()
    mov eax, 10     ; SYS_EXIT
    int 0x30

    ; Should never be reached
.halt:
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
