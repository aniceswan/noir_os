global process_context_switch

; process_context_switch(struct Context ctx)
;
; cdecl argument layout (context passed by value):
;   [esp]      return address
;   [esp+4]    ctx
;
; Context field offsets from ctx base:
;   +0  cpu.index.edi      +4  cpu.index.esi
;   +8  cpu.stack.ebp      +12 cpu.stack.esp (user esp)
;   +16 cpu.general.ebx    +20 cpu.general.edx
;   +24 cpu.general.ecx    +28 cpu.general.eax
;   +32 cpu.segment.gs     +36 cpu.segment.fs
;   +40 cpu.segment.es     +44 cpu.segment.ds
;   +48 eip                +52 eflags
;   +56 page_directory_virtual_addr (CR3 is switched from C)
;
; Build the iret frame (ss, user esp, eflags, cs, eip), load all registers
; from the context and return to ring 3 with iret.
section .text
process_context_switch:
    mov edx, esp

    ; Inter-privilege iret frame
    push dword 0x23                ; user ss (0x20 | 0x3)
    mov eax, [edx + 4 + 12]        ; ctx.cpu.stack.esp
    push eax                       ; user esp
    mov eax, [edx + 4 + 52]        ; ctx.eflags
    push eax
    push dword 0x1B                ; user cs (0x18 | 0x3)
    mov eax, [edx + 4 + 48]        ; ctx.eip
    push eax

    ; Segment registers
    mov eax, [edx + 4 + 44]        ; ctx.cpu.segment.ds
    mov ds, ax
    mov eax, [edx + 4 + 40]        ; ctx.cpu.segment.es
    mov es, ax
    mov eax, [edx + 4 + 36]        ; ctx.cpu.segment.fs
    mov fs, ax
    mov eax, [edx + 4 + 32]        ; ctx.cpu.segment.gs
    mov gs, ax

    ; General purpose & index registers (edx must be loaded last)
    mov edi, [edx + 4 + 0]
    mov esi, [edx + 4 + 4]
    mov ebp, [edx + 4 + 8]
    mov ebx, [edx + 4 + 16]
    mov ecx, [edx + 4 + 24]
    mov eax, [edx + 4 + 28]
    mov edx, [edx + 4 + 20]

    iret

section .note.GNU-stack noalloc noexec nowrite progbits
