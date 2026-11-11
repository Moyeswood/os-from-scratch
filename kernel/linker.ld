SECTIONS {
    . = 0x10000;
    _kernel_start = .;
    .text : {
        *(.text.start)
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    /* 镜像实际加载结束位置 (供内存管理器只预留镜像本身) */
    _kernel_image_end = .;
    /* Legacy 0xA0000-0xFFFFF is VGA/BIOS space, not general RAM. Keep
       the large kernel BSS in ordinary memory below the page tables. */
    . = 0x00200000;
    _bss_start = .;
    .bss : { *(.bss) }
    _kernel_end = .;
}
