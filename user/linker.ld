/* 用户程序链接脚本: 代码/数据全部落在 32MB 起的用户区 (U/S=1 页)。
   _start 放 .text.start 保证排在镜像最前面 (入口 = 0x2000000 = blob 首字节)。 */
ENTRY(_start)
SECTIONS {
    . = 0x2000000;
    .text   : { *(.text.start) *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
    /* objcopy -O binary stops at the last PROGBITS byte. Emit one byte after
       .bss so the zero-filled image includes all allocated static storage;
       otherwise a larger .bss leaves its tail unmapped in the flat loader. */
    .image  : { BYTE(0) }
}
