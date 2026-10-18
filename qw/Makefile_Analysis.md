---
tags:
  - OSDev
  - Makefile
  - BuildSystem
  - Toolchain
date: 2026-10-04
---

# 🏗️ x86 操作系统构建引擎：Makefile 逐行解剖

> [!abstract] 阅读指南
> Makefile 不是简单的"编译命令堆砌"，它是连接**源码、编译器、链接器和硬件模拟器**的桥梁。本文档将拆解每一个参数背后的底层原理。

---

## 🧩 1. 定义工具链与目录结构

> [!info] 核心思想
> 将工具、路径和产物分离，方便日后修改。

```makefile
# ================= 1. 定义工具链 =================
ASM = nasm                # 汇编编译器：NASM
CC = gcc                  # C 语言编译器：GCC
LD = ld                   # 链接器：GNU ld
OBJCOPY = objcopy         # 目标文件复制工具
QEMU = qemu-system-i386   # 硬件模拟器

# ================= 2. 定义目录结构 =================
BOOT_DIR = boot           # 16 位引导代码源码目录
KERNEL_DIR = kernel       # 32 位内核代码源码目录
BUILD_DIR = build         # 编译产物目录

# ================= 3. 定义目标文件路径 =================
BOOT_BIN = $(BUILD_DIR)/boot.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img

# ================= 4. 定义内核的依赖对象 =================
# 【核心】：必须把 entry.o 放在第一个，保证 CPU 跳转到 0x10000 时执行的是 _start
KERNEL_OBJS = $(KERNEL_DIR)/entry.o $(KERNEL_DIR)/kernel.o $(KERNEL_DIR)/serial.o
```

---

## ⚙️ 2. 核心构建规则 (Rules)

> [!tip] Makefile 语法提示
> - `目标: 依赖` 表示"如果要生成目标，必须先有依赖"。
> - `$<` 代表第一个依赖，`$@` 代表目标，`$^` 代表所有依赖。
> - **命令行前面的缩进必须是真实的 Tab 键！**

```makefile
all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@
	# -f bin：输出纯二进制格式，BIOS 只认纯粹的 512 字节机器码。

$(KERNEL_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(ASM) -f elf32 $< -o $@
	# -f elf32：输出 32 位 ELF，以便链接器解析 extern 符号。

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) -m32 -ffreestanding -fno-pie -nostdlib -c $< -o $@
	# -m32: 生成 32 位代码。
	# -ffreestanding: 独立环境，不假设存在操作系统。
	# -fno-pie: 禁用位置无关代码，裸机需要绝对地址。
	# -nostdlib: 不链接 C 标准库，裸机没有 libc。

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker.ld $^ -o $@
	# -T linker.ld: 使用自定义链接脚本，强制代码起始地址为 0x10000。

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	# 剥离 ELF 的元数据，只保留纯机器码。

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=1024 count=1440 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none
	# 拼装并撑大到 QEMU 要求的标准 1.44MB 软盘镜像。

run: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio

debug: $(OS_IMG)
	$(QEMU) -fda $< -s -S -serial stdio
	# -s: 开启 GDB 1234 端口。 -S: 启动时冻结 CPU。

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean
```

---

## 🎯 总结：构建的流水线

> [!quote] 数据流向
> 源码 -> 编译 -> 链接 -> 剥壳 -> 拼装 -> 镜像

1. **boot.asm** --(nasm -f bin)--> **boot.bin** (512字节纯二进制)
2. **entry.asm** --(nasm -f elf32)--> **entry.o** (32位ELF目标文件)
3. **\*.c** --(gcc -m32 -nostdlib)--> **\*.o** (32位ELF目标文件)
4. **\*.o** --(ld -T linker.ld)--> **kernel.elf** (起始地址钉死在 0x10000)
5. **kernel.elf** --(objcopy)--> **kernel.bin** (纯机器码)
6. **boot.bin + kernel.bin** --(dd)--> **os.img** (1.44MB 标准软盘镜像)
