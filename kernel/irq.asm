[BITS 32]
section .text

extern keyboard_handler
extern timer_handler

global irq0_handler
irq0_handler:
    pusha
    call timer_handler
    popa

    mov al, 0x20
    out 0x20, al

    iret

global irq1_handler
irq1_handler:
    pusha
    call keyboard_handler
    popa

    mov al, 0x20
    out 0x20, al

    iret
