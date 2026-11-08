ASM = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-i386

# 当前系统 GCC 在 -m32 时仍可能默认 x86-64/SSE2。NewOS 尚未启用
# CR4.OSFXSR/OSXMMEXCPT，编译器一旦生成 XMM 指令就会触发 #UD。
CFLAGS32 = -m32 -march=i386 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
           -msoft-float -fno-tree-vectorize \
           -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
           -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib

BOOT_DIR = boot
KERNEL_DIR = kernel
BUILD_DIR = build

BOOT_BIN = $(BUILD_DIR)/boot.bin
TRAMP_BIN = $(BUILD_DIR)/rm_trampoline.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img
DISK_IMG = $(BUILD_DIR)/disk.img

KERNEL_OBJS = $(KERNEL_DIR)/entry.o \
              $(KERNEL_DIR)/kernel.o \
              $(KERNEL_DIR)/serial.o \
              $(KERNEL_DIR)/ata.o \
              $(KERNEL_DIR)/vga.o \
              $(KERNEL_DIR)/idt.o \
              $(KERNEL_DIR)/pic.o \
              $(KERNEL_DIR)/irq.o \
              $(KERNEL_DIR)/keyboard.o \
              $(KERNEL_DIR)/timer.o \
              $(KERNEL_DIR)/memory.o \
              $(KERNEL_DIR)/heap.o \
              $(KERNEL_DIR)/paging.o \
              $(KERNEL_DIR)/shell.o \
              $(KERNEL_DIR)/graphics.o \
              $(KERNEL_DIR)/tetris.o \
              $(KERNEL_DIR)/mario.o \
              $(KERNEL_DIR)/font.o \
              $(KERNEL_DIR)/fs.o \
              $(KERNEL_DIR)/fd.o \
              $(KERNEL_DIR)/editor.o \
              $(KERNEL_DIR)/cc.o \
              $(KERNEL_DIR)/splash.o \
              $(KERNEL_DIR)/splash_asm.o \
              $(KERNEL_DIR)/task.o \
              $(KERNEL_DIR)/switch.o \
              $(KERNEL_DIR)/realmode.o \
              $(KERNEL_DIR)/rm_entry.o \
              $(KERNEL_DIR)/rm_embed.o \
              $(KERNEL_DIR)/gdt.o \
              $(KERNEL_DIR)/exc.o \
              $(KERNEL_DIR)/exc_asm.o \
              $(KERNEL_DIR)/user.o \
              $(KERNEL_DIR)/proc.o \
              $(KERNEL_DIR)/vm.o \
              $(KERNEL_DIR)/exec.o \
              $(KERNEL_DIR)/user_asm.o \
              $(KERNEL_DIR)/user_progs.o \
              $(KERNEL_DIR)/user_embed.o

# 用户程序 (宿主 gcc 编译 → 链接到 0x2000000 → flat bin → incbin)
# 里程碑 15 + 本地 allocator 演示；elfdemo 作为独立 ELF 镜像嵌入
USER_PROGS = hello count crash div0 echo badptr forkdemo spinner vmtest vmguard \
             execdemo execbad waittest execargv memdemo allocdemo filedemo fdstdin \
             sh fdpipe
USER_BINS  = $(patsubst %,$(BUILD_DIR)/%.bin,$(USER_PROGS))
ELF_DEMO   = $(BUILD_DIR)/elfdemo.elf

all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(TRAMP_BIN): $(KERNEL_DIR)/rm_trampoline.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(KERNEL_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/irq.o: $(KERNEL_DIR)/irq.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/switch.o: $(KERNEL_DIR)/switch.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/splash_asm.o: $(KERNEL_DIR)/splash.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/rm_entry.o: $(KERNEL_DIR)/rm_entry.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/rm_embed.o: $(KERNEL_DIR)/rm_embed.asm $(TRAMP_BIN)
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/exc_asm.o: $(KERNEL_DIR)/exc_asm.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/user_asm.o: $(KERNEL_DIR)/user_asm.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/user_embed.o: $(KERNEL_DIR)/user_embed.asm $(USER_BINS) $(ELF_DEMO)
	$(ASM) -f elf32 $< -o $@

# 用户程序: 独立编译成 flat binary, 入口 0x2000000 (见 user/linker.ld)
$(BUILD_DIR)/%.bin: user/%.c user/lib.h user/linker.ld
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS32) -O2 -c $< -o $(BUILD_DIR)/$*.o
	$(LD) -m elf_i386 -T user/linker.ld $(BUILD_DIR)/$*.o -o $(BUILD_DIR)/$*.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$*.elf $@

$(ELF_DEMO): user/elfdemo.c user/lib.h user/elf_linker.ld
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS32) -O2 -c user/elfdemo.c -o $(BUILD_DIR)/elfdemo.o
	$(LD) -m elf_i386 -T user/elf_linker.ld $(BUILD_DIR)/elfdemo.o -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c $(wildcard $(KERNEL_DIR)/*.h)
	$(CC) $(CFLAGS32) -g -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker.ld $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=1024 count=1440 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none

# Persistent filesystem disk. Keep this target idempotent: rebuilds must not
# erase files written by either the OS or the persistence tests.
$(DISK_IMG):
	mkdir -p $(BUILD_DIR)
	dd if=/dev/zero of=$@ bs=1M count=2 status=none

run: $(OS_IMG) $(DISK_IMG)
	$(QEMU) -fda $(OS_IMG) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-serial stdio

debug: $(OS_IMG) $(DISK_IMG)
	$(QEMU) -fda $(OS_IMG) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean test-memory test-vm test-exec test-process test-fd test-shell test-disk test-attached test-suite test-all

test-memory: $(OS_IMG)
	python3 tools/test-memory.py

test-vm: $(OS_IMG)
	python3 tools/test-vm.py

test-exec: $(OS_IMG)
	python3 tools/test-exec.py

test-process: $(OS_IMG)
	python3 tools/test-process.py

test-fd: $(OS_IMG)
	python3 tools/test-fd.py

test-shell: $(OS_IMG)
	python3 tools/test-shell.py

test-disk: $(OS_IMG)
	python3 tools/test-disk.py

test-attached: $(OS_IMG)
	python3 tools/run-attached-tests.py

test-suite: $(OS_IMG)
	python3 tools/test-memory.py
	python3 tools/test-splash.py
	python3 tools/test-font.py
	python3 tools/test-fs.py
	python3 tools/test-disk.py
	python3 tools/test-edit.py
	python3 tools/test-cc.py
	python3 tools/test-user.py
	python3 tools/test-proc.py
	python3 tools/test-memproc.py
	python3 tools/test-alloc.py
	python3 tools/test-sched.py
	python3 tools/test-vm.py
	python3 tools/test-exec.py
	python3 tools/test-process.py
	python3 tools/test-fd.py
	python3 tools/test-shell.py
	python3 tools/run-attached-tests.py

test-all: test-suite
