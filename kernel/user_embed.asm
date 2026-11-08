; ============================================================
;  用户程序镜像嵌入 (里程碑 10; 里程碑 13: + forkdemo/spinner/vmtest/vmguard)
;
;  user/*.c 由主机 gcc -m32 编译链接成 flat bin (入口 0x2000000),
;  这里 incbin 进内核, run 命令再拷到用户区执行。
;  (和 rm_embed.asm 内嵌实模式跳板同一手法。)
; ============================================================

[BITS 32]
section .rodata

global user_hello_start
user_hello_start: incbin "build/hello.bin"
global user_hello_end
user_hello_end:

global user_count_start
user_count_start: incbin "build/count.bin"
global user_count_end
user_count_end:

global user_crash_start
user_crash_start: incbin "build/crash.bin"
global user_crash_end
user_crash_end:

global user_div0_start
user_div0_start: incbin "build/div0.bin"
global user_div0_end
user_div0_end:

global user_echo_start
user_echo_start: incbin "build/echo.bin"
global user_echo_end
user_echo_end:

global user_badptr_start
user_badptr_start: incbin "build/badptr.bin"
global user_badptr_end
user_badptr_end:

global user_forkdemo_start
user_forkdemo_start: incbin "build/forkdemo.bin"
global user_forkdemo_end
user_forkdemo_end:

global user_spinner_start
user_spinner_start: incbin "build/spinner.bin"
global user_spinner_end
user_spinner_end:

global user_memdemo_start
user_memdemo_start: incbin "build/memdemo.bin"
global user_memdemo_end
user_memdemo_end:

global user_allocdemo_start
user_allocdemo_start: incbin "build/allocdemo.bin"
global user_allocdemo_end
user_allocdemo_end:

global user_filedemo_start
user_filedemo_start: incbin "build/filedemo.bin"
global user_filedemo_end
user_filedemo_end:

global user_fdstdin_start
user_fdstdin_start: incbin "build/fdstdin.bin"
global user_fdstdin_end
user_fdstdin_end:

global user_sh_start
user_sh_start: incbin "build/sh.bin"
global user_sh_end
user_sh_end:

global user_fdpipe_start
user_fdpipe_start: incbin "build/fdpipe.bin"
global user_fdpipe_end
user_fdpipe_end:

global user_fscheck_start
user_fscheck_start: incbin "build/fscheck.bin"
global user_fscheck_end
user_fscheck_end:

global user_vmtest_start
user_vmtest_start: incbin "build/vmtest.bin"
global user_vmtest_end
user_vmtest_end:

global user_vmguard_start
user_vmguard_start: incbin "build/vmguard.bin"
global user_vmguard_end
user_vmguard_end:

global user_execdemo_start
user_execdemo_start: incbin "build/execdemo.bin"
global user_execdemo_end
user_execdemo_end:

global user_execbad_start
user_execbad_start: incbin "build/execbad.bin"
global user_execbad_end
user_execbad_end:

global user_waittest_start
user_waittest_start: incbin "build/waittest.bin"
global user_waittest_end
user_waittest_end:

global user_execargv_start
user_execargv_start: incbin "build/execargv.bin"
global user_execargv_end
user_execargv_end:

global user_elfdemo_elf_start
user_elfdemo_elf_start: incbin "build/elfdemo.elf"
global user_elfdemo_elf_end
user_elfdemo_elf_end:
