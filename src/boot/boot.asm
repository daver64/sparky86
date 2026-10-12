format ELF          ; Directs FASM to output a standard linkable object file

section '.text' align 8 ; <-- Add 'align 8' here to allow alignment directives inside

public start        ; Exposes our execution start entry hook to the linker
extrn _start        ; Correct FASM spelling for importing an external symbol
extrn keyboard_isr  ; C handler for IRQ vector 0x21

IDT_ENTRIES = 0x22  ; vectors 0x00-0x21 (0x20 vblank, 0x21 keyboard)

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

    ; Point every vector at default_isr, then install the keyboard handler
    mov ebx, default_isr
    mov edi, idt_start
    mov ecx, IDT_ENTRIES
.fill_idt:
    call set_gate
    add edi, 8
    loop .fill_idt

    mov ebx, vblank_stub
    mov edi, idt_start + 0x20 * 8
    call set_gate

    mov ebx, keyboard_stub
    mov edi, idt_start + 0x21 * 8
    call set_gate

    lidt [idt_descriptor]
    sti                 ; Interrupts on: the host can now deliver vblank and keyboard IRQs

    call _start         ; Execute our GCC application framework entry loop

.halt_loop:
    cli                 ; _start returned: with IF clear, hlt stops the emulator
    hlt
    jmp .halt_loop

; edi = gate address, ebx = handler offset: builds a 32-bit interrupt gate (selector 0x08)
set_gate:
    mov [edi], bx
    mov word [edi + 2], 0x08
    mov word [edi + 4], 0x8E00
    mov eax, ebx
    shr eax, 16
    mov [edi + 6], ax
    ret

; Unhandled vector or CPU fault: halt, which stops the guest
default_isr:
    hlt
    jmp default_isr

; Vertical blank: nothing to do, the interrupt just wakes a waiting hlt
vblank_stub:
    iretd

keyboard_stub:
    pushad
    cld
    call keyboard_isr
    popad
    iretd

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

idt_descriptor:
    dw IDT_ENTRIES * 8 - 1
    dd idt_start

align 8
idt_start:
    db IDT_ENTRIES * 8 dup 0
