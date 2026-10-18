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

KERNEL_OBJS = $(KERNEL_DIR)/entry.o $(KERNEL_DIR)/kernel.o $(KERNEL_DIR)/serial.o $(KERNEL_DIR)/vga.o $(KERNEL_DIR)/idt.o $(KERNEL_DIR)/pic.o $(KERNEL_DIR)/irq.o $(KERNEL_DIR)/keyboard.o $(KERNEL_DIR)/memory.o $(KERNEL_DIR)/paging.o

all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(KERNEL_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/irq.o: $(KERNEL_DIR)/irq.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -c $< -o $@

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
	$(QEMU) -fda $< -s -S -serial stdio

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean
