[BITS 32]
section .text.start

global _start
extern kernel_main
extern _bss_start
extern _kernel_end

_start:
    ; 修复: BSS 被链接脚本上提到 0x200000, 不在镜像文件内,
    ; 必须显式清零 (此前依赖 QEMU 初启零填充, 重启/真实硬件上不可靠)
    mov edi, _bss_start
    mov ecx, _kernel_end
    sub ecx, edi
    xor eax, eax
    shr ecx, 2
    rep stosd

    call kernel_main
    jmp $ 
