format ELF          ; Directs FASM to output a standard linkable object file

section '.text' align 8 ; <-- Add 'align 8' here to allow alignment directives inside

public start        ; Exposes our execution start entry hook to the linker
extrn _start        ; Correct FASM spelling for importing an external symbol

use16               ; Emit initial real-mode instruction sets
start:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Explicit instruction override to perform a 16-to-32 far jump inside ELF
    db 0x66, 0xEA       
    dd protected_entry  
    dw 0x08             

use32               ; Move execution strictly into native 32-bit instructions
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x0000FFFF ; Map out a clean stack space at the top limit of 64KB

    call _start         ; Execute our GCC application framework entry loop

.halt_loop:
    hlt
    jmp .halt_loop

align 8
gdt_start:
    dd 0, 0
; Code Segment Descriptor (Selector Index 0x08)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
; Data Segment Descriptor (Selector Index 0x10)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
