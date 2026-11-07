ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x2000000;
    .text : { *(.text.start) *(.text) } :text
    .rodata : { *(.rodata) *(.rodata.*) } :text
    . = ALIGN(0x1000);
    .data : { *(.data) *(.data.*) } :data
    .bss : { *(.bss) *(.bss.*) *(COMMON) } :data
}
