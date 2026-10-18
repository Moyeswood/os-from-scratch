[BITS 32]
section .text

extern keyboard_handler

global irq1_handler
irq1_handler:
    pusha
    call keyboard_handler
    popa
    
    mov al, 0x20
    out 0x20, al
    
    iret
