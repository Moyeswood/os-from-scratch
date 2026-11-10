SECTIONS {
    . = 0x10000;
    _kernel_start = .;
    .text : { 
        *(.text.start)
        *(.text) 
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    /* Legacy 0xA0000-0xFFFFF is VGA/BIOS space, not general RAM. Keep
       the large kernel BSS in ordinary memory below the page tables. */
    . = 0x00200000;
    .bss : { *(.bss) }
    _kernel_end = .;
}
