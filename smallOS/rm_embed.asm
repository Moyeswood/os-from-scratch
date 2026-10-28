section .rodata
global rm_trampoline_bin
global rm_trampoline_bin_end
rm_trampoline_bin:
    incbin "build/rm_trampoline.bin"
rm_trampoline_bin_end:
