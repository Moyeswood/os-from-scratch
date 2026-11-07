ASM = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-i386

BOOT_DIR = boot
KERNEL_DIR = kernel
BUILD_DIR = build

BOOT_BIN = $(BUILD_DIR)/boot.bin
TRAMP_BIN = $(BUILD_DIR)/rm_trampoline.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img

KERNEL_OBJS = $(KERNEL_DIR)/entry.o \
              $(KERNEL_DIR)/kernel.o \
              $(KERNEL_DIR)/serial.o \
              $(KERNEL_DIR)/vga.o \
              $(KERNEL_DIR)/idt.o \
              $(KERNEL_DIR)/pic.o \
              $(KERNEL_DIR)/irq.o \
              $(KERNEL_DIR)/keyboard.o \
              $(KERNEL_DIR)/timer.o \
              $(KERNEL_DIR)/memory.o \
              $(KERNEL_DIR)/paging.o \
              $(KERNEL_DIR)/shell.o \
              $(KERNEL_DIR)/graphics.o \
              $(KERNEL_DIR)/tetris.o \
              $(KERNEL_DIR)/mario.o \
              $(KERNEL_DIR)/font.o \
              $(KERNEL_DIR)/fs.o \
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
              $(KERNEL_DIR)/user_asm.o \
              $(KERNEL_DIR)/user_progs.o \
              $(KERNEL_DIR)/user_embed.o

# 用户程序 (宿主 gcc 编译 → 链接到 0x2000000 → flat bin → incbin)
# 里程碑 13: + allocdemo (空闲链表 + 合并 + 堆尾收缩)
USER_PROGS = hello count crash div0 echo badptr forkdemo spinner memdemo allocdemo
USER_BINS  = $(patsubst %,$(BUILD_DIR)/%.bin,$(USER_PROGS))

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

$(KERNEL_DIR)/user_embed.o: $(KERNEL_DIR)/user_embed.asm $(USER_BINS)
	$(ASM) -f elf32 $< -o $@

# 用户程序: 独立编译成 flat binary, 入口 0x2000000 (见 user/linker.ld)
$(BUILD_DIR)/%.bin: user/%.c user/lib.h user/linker.ld
	mkdir -p $(BUILD_DIR)
	$(CC) -m32 -O2 -mno-mmx -mno-sse -mno-sse2 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -c $< -o $(BUILD_DIR)/$*.o
	$(LD) -m elf_i386 -T user/linker.ld $(BUILD_DIR)/$*.o -o $(BUILD_DIR)/$*.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$*.elf $@

$(KERNEL_DIR)/editor.o: $(KERNEL_DIR)/editor.c
	$(CC) -m32 -g -mno-mmx -mno-sse -mno-sse2 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -c $< -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) -m32 -g -ffreestanding -fno-pie -fno-stack-protector -nostdlib -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker.ld $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=1024 count=1440 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none

run: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio

debug: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean
