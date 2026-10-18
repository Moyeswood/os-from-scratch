ASM = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-i386

BOOT_DIR = boot
KERNEL_DIR = kernel
BUILD_DIR = build

BOOT_BIN = $(BUILD_DIR)/boot.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img

# 包含所有内核目标文件
KERNEL_OBJS = $(KERNEL_DIR)/entry.o \
              $(KERNEL_DIR)/kernel.o \
              $(KERNEL_DIR)/serial.o \
              $(KERNEL_DIR)/vga.o \
              $(KERNEL_DIR)/idt.o \
              $(KERNEL_DIR)/pic.o \
              $(KERNEL_DIR)/irq.o \
              $(KERNEL_DIR)/keyboard.o \
              $(KERNEL_DIR)/memory.o \
              $(KERNEL_DIR)/paging.o \
              $(KERNEL_DIR)/shell.o

all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(KERNEL_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/irq.o: $(KERNEL_DIR)/irq.asm
	$(ASM) -f elf32 $< -o $@

# 【关键】：C 语言编译规则。
# 添加了 -g (生成调试信息) 和 -fno-stack-protector (禁用栈保护)
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

# 【新增】：调试目标。启动 QEMU 并暂停 CPU，等待 GDB 连接
debug: $(OS_IMG)
	@echo "=========================================="
	@echo " QEMU 已启动，正在等待 GDB 连接 (端口 1234)..."
	@echo " 请在【另一个终端】中运行以下命令："
	@echo " gdb -ex 'target remote localhost:1234' -ex 'file build/kernel.elf'"
	@echo "=========================================="
	$(QEMU) -fda $< -serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean
