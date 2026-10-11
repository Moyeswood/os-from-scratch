# OS From Scratch 🖥️

[English](#english) | [中文](#中文)

---

# 中文

> 从零开始实现一个简单的操作系统，深入学习计算机底层知识

<a id="中文"></a>

## 📖 项目介绍

**OS From Scratch** 是一个以学习为目的的实战项目，通过从头开始实现一个微型操作系统，深入理解计算机底层原理。

这不是在学习某个现成的操作系统（如 Linux），而是从引导加载器开始，逐步实现内核、驱动程序、内存管理等核心功能，让你真正理解：

- 🔌 计算机启动的全过程
- 🏗️ 操作系统的内部架构
- ⚙️ 硬件与软件如何交互
- 📚 编译链接的底层原理
- 🎯 为什么要这样设计系统

### 🎓 适合谁

- 正在学习操作系统原理的学生
- 想深入理解系统底层的开发者
- 对编译原理感兴趣的人
- 想提升系统编程能力的工程师

---

## 📁 项目结构

```
os-from-scratch/
│
├── bootloader/                 # 引导加载器
│   ├── boot.asm               # MBR 引导代码（第一阶段）
│   ├── kernel_load.asm        # 内核加载器（第二阶段）
│   └── gdt.asm                # 全局描述符表
│
├── kernel/                     # 内核代码
│   ├── kernel.c               # 内核主程序
│   ├── kernel.asm             # 内核初始化汇编
│   ├── main.c                 # 主函数入口
│   └── drivers/               # 驱动程序
│       ├── disk.c             # 磁盘驱动
│       ├── serial.c           # 串行通信驱动
│       ├── keyboard.c         # 键盘驱动
│       └── vga.c              # VGA 显示驱动
│
├── arch/                       # 体系结构相关代码
│   ├── x86/
│   │   ├── cpu.asm            # CPU 相关操作
│   │   ├── interrupt.asm      # 中断处理
│   │   └── memory.c           # 内存管理
│   └── include/
│       ├── cpu.h
│       ├── memory.h
│       └── interrupt.h
│
├── lib/                        # 库文件
│   ├── string.c               # 字符串函数
│   ├── stdio.c                # 输入输出
│   ├── stdlib.c               # 标准库
│   └── include/
│       ├── string.h
│       ├── stdio.h
│       └── types.h
│
├── tools/                      # 构建工具
│   ├── build.sh               # 编译脚本
│   ├── qemu.sh                # QEMU 运行脚本
│   ├── image.py               # 镜像生成工具
│   └── link.ld                # 链接脚本
│
├── docs/                       # 文档
│   ├── architecture.md        # 系统架构设计
│   ├── bootloader.md          # 引导过程详解
│   ├── memory_layout.md       # 内存布局
│   ├── interrupt.md           # 中断处理
│   ├── gdt_idt.md             # GDT 和 IDT
│   └── development_log.md     # 开发日志
│
├── tests/                      # 测试
│   ├── unit/
│   └── integration/
│
├── Makefile                    # 编译配置
├── README.md                   # 项目说明（本文件）
└── LICENSE                     # MIT 开源许可

```

---

## 🚀 快速开始

### ✅ 前置要求

**Linux/macOS：**
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y nasm gcc gcc-multilib binutils qemu

# macOS
brew install nasm gcc binutils qemu
```

**Windows：**
- 推荐使用 WSL2（Windows Subsystem for Linux 2）
- 或者使用虚拟机运行 Linux
- [WSL2 安装指南](https://docs.microsoft.com/en-us/windows/wsl/install)

### 📦 克隆项目

```bash
git clone https://github.com/Moyeswood/os-from-scratch.git
cd os-from-scratch
```

### 🔨 编译

```bash
# 方法 1：使用 Makefile
make

# 方法 2：使用编译脚本
chmod +x tools/build.sh
./tools/build.sh
```

### 🎮 运行

```bash
# 使用 QEMU 虚拟机运行
make qemu

# 或者
./tools/qemu.sh

# 退出 QEMU：按 Ctrl+A，然后按 X
```

### 📊 查看生成的镜像

```bash
# 列出编译产物
ls -lh build/

# 查看镜像大小
hexdump -C build/os.img | head -20
```

---

## 📚 学习路线

### 🎯 第一阶段：引导加载器（预计 1-2 周）

**目标**：理解计算机启动过程，实现基本的磁盘读取

- [x] 理解 BIOS 启动过程
- [x] 学习 x86 实模式编程
- [x] 实现 MBR 引导代码
- [x] 实现磁盘读取函数（int 0x13）
- [ ] 实现字符串打印函数
- [ ] 进入第二阶段加载器

**关键概念**：
- 实模式、内存寻址、BIOS 中断、磁盘 I/O
- 寄存器、栈、函数调用约定

**相关文件**：`bootloader/`  
**学习资源**：`docs/bootloader.md`

**代码示例**：
```assembly
; MBR 引导代码基本框架
[org 0x7c00]

mov [BOOT_DRIVE], dl    ; 保存驱动器号
mov bx, 0x7e00          ; 加载地址
call disk_load          ; 读取磁盘
jmp 0x7e00              ; 跳转到加载的代码

; ... 子程序实现 ...

times 510 - ($ - $$) db 0  ; 填充
dw 0xaa55                   ; 引导标志
```

---

### 🎯 第二阶段：内核基础（预计 1-2 周）

**目标**：从实模式切换到保护模式，建立内核框架

- [ ] 理解保护模式的优势
- [ ] 实现 GDT（全局描述符表）
- [ ] 切换到 32 位保护模式
- [ ] 实现简单的 VGA 输出
- [ ] 编写 C 语言内核主函数
- [ ] 建立汇编与 C 的交互

**关键概念**：
- GDT、段描述符、保护模式、栈帧、调用约定
- 汇编与 C 的混合编程

**相关文件**：`bootloader/gdt.asm`, `kernel/kernel.asm`

**代码示例**：
```nasm
; 切换到保护模式
cli                           ; 禁用中断
lgdt [gdt_descriptor]        ; 加��� GDT
mov eax, cr0                 ; 
or eax, 1                    ; 设置 PE 位
mov cr0, eax                 ; 启用保护模式
jmp CODE_SEG:init_pm         ; 跳转到 32 位代码
```

---

### 🎯 第三阶段：中断和异常（预计 1-2 周）

**目标**：实现 IDT，处理硬件和软件中断

- [ ] 理解 IDT（中断描述符表）
- [ ] 实现 ISR（中断服务程序）
- [ ] 初始化 8259 中断控制器
- [ ] 处理时钟中断（IRQ0）
- [ ] 处理键盘中断（IRQ1）
- [ ] 实现异常处理

**关键概念**：
- 中断、异常、中断门、陷阱门
- IRQ、向量号、优先级

**相关文件**：`arch/x86/interrupt.asm`, `docs/interrupt.md`

---

### 🎯 第四阶段：内存管理（预计 2-3 周）

**目标**：实现虚拟内存和简单的内存分配器

- [ ] 理解虚拟内存概念
- [ ] 实现分页机制（Page Table）
- [ ] 实现堆内存分配器（malloc/free）
- [ ] 实现栈扩展
- [ ] 内存保护和权限检查

**关键概念**：
- 分页、页表、TLB、缺页异常
- 堆、栈、内存碎片

**相关文件**：`arch/x86/memory.c`, `docs/memory_layout.md`

---

### 🎯 第五阶段：驱动程序（预计 2-3 周）

**目标**：实现基本的硬件驱动

- [ ] 磁盘驱动（IDE/ATA）
- [ ] 键盘和鼠标驱动
- [ ] 串行通信驱动
- [ ] 简单的文件系统（FAT12）

**相关文件**：`kernel/drivers/`

---

### 🎯 第六阶段：进程管理（预计 2-3 周）

**目标**：实现多进程和基本的调度

- [ ] 进程控制块（PCB）设计
- [ ] 进程创建和销毁
- [ ] 上下文切换
- [ ] 调度算法（轮转调度）
- [ ] 进程间通信（IPC）

---

### 🎯 第七阶段：用户程序（预计 1-2 周）

**目标**：实现用户态和系统调用

- [ ] 用户态与内核态切换
- [ ] 系统调用接口设计
- [ ] Shell 命令行解释器
- [ ] 简单的用户程序

---

## 💡 核心特性

### ✅ 已实现
- [x] MBR 引导加载器基础
- [x] BIOS 磁盘读取（int 0x13）
- [x] 内存加载机制
- [x] 基本的错误处理

### 🔄 进行中
- [ ] 保护模式切换
- [ ] GDT/IDT 实现
- [ ] 中断处理器

### 📋 计划中
- [ ] 虚拟内存和分页
- [ ] 多进程支持
- [ ] 文件系统（FAT12）
- [ ] Shell 和用户程序
- [ ] 网络支持（可选）

---

## 🏗️ 系统架构概览

```
┌─────────────────────────────────────┐
│      用户程序 (User Space)          │  Ring 3
│  Shell | App1 | App2 | ...          │
├─────────────────────────────────────┤
│          系统调用接口 (Syscall)      │
├─────────────────────────────────────┤
│       内核 (Kernel Space)            │  Ring 0
│  Process | Memory | File | Driver    │
├─────────────────────────────────────┤
│      中断/异常处理 (Interrupt)       │
├─────────────────────────────────────┤
│     硬件抽象层 (HAL / CPU)           │
├─────────────────────────────────────┤
│  CPU | Memory | Disk | Keyboard ... │  Hardware
└─────────────────────────────────────┘
```

---

## 📖 重要概念解释

### 实模式 vs 保护模式

| 特性 | 实模式 | 保护模式 |
|------|--------|---------|
| 寻址范围 | 1 MB (20 位) | 4 GB (32 位) |
| 内存保护 | ❌ 无 | ✅ 有 |
| 特权级别 | Ring 0 only | Ring 0-3 |
| 多任务支持 | ❌ 有限 | ✅ 完整 |
| 中断方式 | BIOS 中断 | IDT |
| 虚拟内存 | ❌ 无 | ✅ 有 |

### 段和偏移量

```
实模式地址计算：
物理地址 = 段基地址 × 16 + 偏移量
例：段 0x7c0，偏移 0x00 = 0x7c0 * 16 + 0 = 0x7c00

保护模式：
使用段描述符和分页，更复杂但更安全
```

### 中断和异常

```
异常（Exception）- CPU 检测到的错误条件
├── Fault    - 可恢复故障（如缺页异常）
├── Trap     - 陷阱（如断点）
└── Abort    - 不可恢复中止（如双重故障）

中断（Interrupt）- 外设请求
├── 硬件中断（Hardware IRQ）
│   ├── IRQ0  - 时钟
│   ├── IRQ1  - 键盘
│   ├── IRQ3  - 串口 COM2
│   ├── IRQ4  - 串口 COM1
│   └── ...
└── 软件中断（Software INT）- int 指令
    ├── int 0x10 - BIOS 显示
    ├── int 0x13 - BIOS 磁盘
    └── ...
```

### GDT 和 IDT

**GDT (全局描述符表)**：
- 用于保护模式
- 定义内存段的属性（基地址、界限、权限等）
- 每个段描述符 8 字节
- 最多 8192 个条目

**IDT (中断描述符表)**：
- 类似于 GDT，但用于中断处理
- 存储中断处理程序的入口地址
- 每个条目 8 字节
- 最多 256 个中断

---

## 🛠️ 开发指南

### 代码规范

**汇编代码（NASM 语法）**：
```assembly
; 函数示例
; 参数：EAX = 值 1，EBX = 值 2
; 返回值：EAX = 结果
global my_function
my_function:
    push rbp                ; 保存旧栈帧指针
    mov rbp, rsp            ; 建立新栈帧
    
    ; 函数体
    add eax, ebx
    
    pop rbp                 ; 恢复栈帧指针
    ret
```

**C 代码**：
```c
// 遵循 Linux kernel coding style
#include "stdio.h"

// 函数名清晰
int kernel_main(void) {
    printk("OS From Scratch\n");
    printk("Hello, World!\n");
    
    return 0;
}
```

### 提交规范

遵循 Conventional Commits 格式：

```bash
# 功能
git commit -m "feat: implement disk_load function"

# 修复
git commit -m "fix: resolve stack imbalance in bootloader"

# 文档
git commit -m "docs: add bootloader documentation"

# 重构
git commit -m "refactor: optimize memory allocator"

# 测试
git commit -m "test: add unit tests for string functions"
```

### 分支策略

- `main` - 稳定版本
- `develop` - 开发分支
- `feature/*` - 功能分支（如 `feature/bootloader`）
- `bugfix/*` - 修复分支（如 `bugfix/stack-issue`）

```bash
# 创建功能分支
git checkout -b feature/bootloader

# 开发完成后提交 PR
git push origin feature/bootloader
```

---

## 🧪 测试

### 编译测试

```bash
# 检查是否能成功编译
make clean
make
echo "编译成功"
```

### 运行测试

```bash
# 在 QEMU 中测试
make qemu

# 自动化测试
make test
```

### 调试

```bash
# 使用 QEMU 的 GDB 调试
make debug

# 在 gdb 中
(gdb) target remote localhost:1234
(gdb) file build/kernel.elf
(gdb) break main
(gdb) continue
```

---

## 📚 参考资源

### 📖 推荐书籍

1. **《Orange'S：一个操作系统的实现》**（橙书）
   - 中文最经典的 OS 书
   - 详细讲解引导、保护模式、中断等
   - 配套代码清晰易懂

2. **《Operating Systems: Three Easy Pieces》**
   - 英文 OS 圣经
   - 讲解原理和设计思想
   - 深度和广度兼具

3. **《The Linux Kernel Module Programming Guide》**
   - Linux 内核模块编程
   - 实战性强

### 🌐 在线教程

- **[OSDev.org - OS Development Wiki](https://wiki.osdev.org/)**
  - 最完整的 OS 开发参考
  - 涵盖 x86、ARM、RISC-V 等架构
  - 必读资源

- **[Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/index.php)**
  - 经典的内核开发教程
  - 适合初学者

- **[JamesM's Kernel Development Tutorials](https://www.jamesmolloy.co.uk/tutorial_html/)**
  - 详细的开发步骤
  - 代码易于理解

### 📘 官方文档

- **Intel x86-64 Architecture Manual**
  - 处理器指令和架构
  - 权威参考

- **Intel Multiprocessor Specification**
  - 多处理器支持

- **System V AMD64 ABI**
  - x86-64 调用约定和 ABI

### 🔧 工具和实用工具

- **NASM（Netwide Assembler）** - x86 汇编器
- **GCC** - C 编译器
- **QEMU** - 虚拟机模拟器
- **GDB** - 调试器
- **Objdump** - 二进制分析工具

---

## 🤝 贡献指南

欢迎提交 Issues 和 Pull Requests！

### 提交 Issue

1. 点击 [Issues](../../issues) 页面的 "New Issue"
2. 选择合适的模板（Bug Report / Feature Request）
3. 清晰描述问题或建议
4. 附加相关代码片段、错误信息、截图等

**Issue 标签**：
- `bug` - 缺陷
- `enhancement` - 改进
- `documentation` - 文档
- `help wanted` - 需要帮助
- `good first issue` - 适合新手

### 提交 PR

1. **Fork 仓库**
   ```bash
   点击 Fork 按钮
   ```

2. **克隆你的 Fork**
   ```bash
   git clone https://github.com/你的用户名/os-from-scratch.git
   cd os-from-scratch
   ```

3. **创建功能分支**
   ```bash
   git checkout -b feature/你的功能名
   ```

4. **提交更改**
   ```bash
   git add .
   git commit -m "feat: 功能描述"
   git push origin feature/你的功能名
   ```

5. **提交 Pull Request**
   - 在 GitHub 上点击 "Compare & pull request"
   - 填写清晰的标题和说明
   - 关联相关的 Issue（使用 `#issue_number`）

**PR 检查清单**：
- [ ] 代码遵循项目风格规范
- [ ] 添加了必要的注释
- [ ] 更新了相关文档
- [ ] 提交信息清晰明确
- [ ] 没有引入警告或错误
- [ ] 在本地测试通过

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源许可。

```
MIT License

Copyright (c) 2026 Moyeswood

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction...
```

---

## 👨‍💻 关于作者

**Moyeswood**

- GitHub: [@Moyeswood](https://github.com/Moyeswood)
- 邮箱：li228459909@163.com

### 项目动机

这个项目诞生于对操作系统底层原理的好奇心。通过一步步实现 OS 的每个组件，我逐渐理解了计算机从启动到运行程序的全过程。希望这个项目也能帮助其他学习者深入理解系统。

---

## 🎯 项目价值

完成这个项目，你将能够：

✅ 理解计算机的启动过程  
✅ 掌握 x86 汇编语言  
✅ 理解操作系统的核心设计  
✅ 学习内存管理和虚拟内存  
✅ 了解中断和异常处理  
✅ 提升系统级编程能力  
✅ 为学习 Linux 内核打下坚实基础  

---

## 🔗 相关资源

- [Linux Kernel Source Code](https://github.com/torvalds/linux)
- [MINIX 3 - 教学型操作系统](http://www.minix3.org/)
- [Pintos - Stanford OS 课程](https://www.scs.stanford.edu/23sp-cs212/)

---

**⭐ 如果这个项目对你有帮助，请给个 Star！**

**📧 有问题？欢迎通过 Issues 或邮件联系！**

---

---

# English

> Build a simple operating system from scratch and master low-level computing concepts

<a id="english"></a>

## 📖 Project Introduction

**OS From Scratch** is a hands-on learning project that implements a minimal operating system from the ground up to deeply understand computer architecture and system design.

Instead of studying an existing OS like Linux, this project starts from the bootloader and progressively implements the kernel, drivers, and memory management—enabling you to truly understand:

- 🔌 How computers boot and start
- 🏗️ Operating system architecture and design
- ⚙️ Hardware-software interaction
- 📚 Compilation and linking fundamentals
- 🎯 Why systems are designed the way they are

### 🎓 Who Should Take This Course

- Students studying operating systems theory
- Developers interested in low-level system programming
- Anyone curious about compiler design and optimization
- Engineers wanting to advance their systems programming skills

---

## 📁 Project Structure

```
os-from-scratch/
│
├── bootloader/                 # Bootloader
│   ├── boot.asm               # MBR boot code (Stage 1)
│   ├── kernel_load.asm        # Kernel loader (Stage 2)
│   └── gdt.asm                # Global Descriptor Table
│
├── kernel/                     # Kernel
│   ├── kernel.c               # Kernel main program
│   ├── kernel.asm             # Kernel assembly setup
│   ├── main.c                 # Main entry point
│   └── drivers/               # Device drivers
│       ├── disk.c             # Disk driver
│       ├── serial.c           # Serial communication driver
│       ├── keyboard.c         # Keyboard driver
│       └── vga.c              # VGA display driver
│
├── arch/                       # Architecture-specific code
│   ├── x86/
│   │   ├── cpu.asm            # CPU operations
│   │   ├── interrupt.asm      # Interrupt handling
│   │   └── memory.c           # Memory management
│   └── include/
│       ├── cpu.h
│       ├── memory.h
│       └── interrupt.h
│
├── lib/                        # Libraries
│   ├── string.c               # String functions
│   ├── stdio.c                # Input/Output
│   ├── stdlib.c               # Standard library
│   └── include/
│       ├── string.h
│       ├── stdio.h
│       └── types.h
│
├── tools/                      # Build tools
│   ├── build.sh               # Build script
│   ├── qemu.sh                # QEMU runner
│   ├── image.py               # Image generator
│   └── link.ld                # Linker script
│
├── docs/                       # Documentation
│   ├── architecture.md        # System architecture
│   ├── bootloader.md          # Boot process explained
│   ├── memory_layout.md       # Memory layout
│   ├── interrupt.md           # Interrupt handling
│   ├── gdt_idt.md             # GDT and IDT reference
│   └── development_log.md     # Development log
│
├── tests/                      # Tests
│   ├── unit/
│   └── integration/
│
├── Makefile                    # Build configuration
├── README.md                   # This file
└── LICENSE                     # MIT License

```

---

## 🚀 Quick Start

### ✅ Prerequisites

**Linux/macOS:**
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y nasm gcc gcc-multilib binutils qemu

# macOS
brew install nasm gcc binutils qemu
```

**Windows:**
- Use WSL2 (Windows Subsystem for Linux 2)
- Or run Linux in a virtual machine
- [WSL2 Installation Guide](https://docs.microsoft.com/en-us/windows/wsl/install)

### 📦 Clone Repository

```bash
git clone https://github.com/Moyeswood/os-from-scratch.git
cd os-from-scratch
```

### 🔨 Build

```bash
# Method 1: Using Makefile
make

# Method 2: Using build script
chmod +x tools/build.sh
./tools/build.sh
```

### 🎮 Run

```bash
# Run in QEMU virtual machine
make qemu

# Or directly
./tools/qemu.sh

# Exit QEMU: Press Ctrl+A, then X
```

### 📊 Check Build Artifacts

```bash
# List compiled files
ls -lh build/

# View image size
hexdump -C build/os.img | head -20
```

---

## 📚 Learning Path

### 🎯 Phase 1: Bootloader (1-2 weeks)

**Goal:** Understand the boot process and implement basic disk reading

- [x] Understand BIOS boot process
- [x] Learn x86 real mode programming
- [x] Implement MBR boot code
- [x] Implement disk read function (int 0x13)
- [ ] Implement string print function
- [ ] Load Stage 2 bootloader

**Key Concepts:**
Real mode, memory addressing, BIOS interrupts, disk I/O, registers, stack, calling conventions

**Files:** `bootloader/`  
**Documentation:** `docs/bootloader.md`

**Code Example:**
```assembly
; MBR bootloader framework
[org 0x7c00]

mov [BOOT_DRIVE], dl    ; Save boot drive
mov bx, 0x7e00          ; Load address
call disk_load          ; Read disk
jmp 0x7e00              ; Jump to loaded code

; ... subroutines ...

times 510 - ($ - $$) db 0  ; Padding
dw 0xaa55                   ; Boot signature
```

---

### 🎯 Phase 2: Kernel Basics (1-2 weeks)

**Goal:** Switch to protected mode and establish kernel framework

- [ ] Understand protected mode advantages
- [ ] Implement GDT (Global Descriptor Table)
- [ ] Switch to 32-bit protected mode
- [ ] Implement basic VGA output
- [ ] Write kernel main in C
- [ ] Mix assembly and C code

**Key Concepts:**
GDT, segment descriptors, protected mode, stack frames, calling conventions

**Files:** `bootloader/gdt.asm`, `kernel/kernel.asm`

---

### 🎯 Phase 3: Interrupts & Exceptions (1-2 weeks)

**Goal:** Implement IDT and handle interrupts

- [ ] Understand IDT (Interrupt Descriptor Table)
- [ ] Implement ISR (Interrupt Service Routines)
- [ ] Initialize 8259 PIC
- [ ] Handle timer interrupt (IRQ0)
- [ ] Handle keyboard interrupt (IRQ1)
- [ ] Exception handling

**Key Concepts:**
Interrupts, exceptions, interrupt gates, IRQ vectors, priorities

---

### 🎯 Phase 4: Memory Management (2-3 weeks)

**Goal:** Implement virtual memory and basic heap allocator

- [ ] Understand virtual memory
- [ ] Implement paging (Page Table)
- [ ] Implement heap allocator (malloc/free)
- [ ] Stack expansion
- [ ] Memory protection

**Key Concepts:**
Paging, page tables, TLB, page faults, heap, stack, fragmentation

---

### 🎯 Phase 5: Device Drivers (2-3 weeks)

**Goal:** Implement hardware drivers

- [ ] Disk driver (IDE/ATA)
- [ ] Keyboard and mouse drivers
- [ ] Serial communication driver
- [ ] Simple filesystem (FAT12)

---

### 🎯 Phase 6: Process Management (2-3 weeks)

**Goal:** Implement multiprocessing and scheduling

- [ ] Process Control Block (PCB) design
- [ ] Process creation and termination
- [ ] Context switching
- [ ] Scheduling algorithms
- [ ] Inter-process communication (IPC)

---

### 🎯 Phase 7: User Programs (1-2 weeks)

**Goal:** Implement user mode and system calls

- [ ] User/kernel mode switching
- [ ] System call interface
- [ ] Shell command interpreter
- [ ] Simple user programs

---

## 💡 Key Features

### ✅ Completed
- [x] Basic MBR bootloader
- [x] BIOS disk reading (int 0x13)
- [x] Memory loading mechanism
- [x] Basic error handling

### 🔄 In Progress
- [ ] Protected mode switching
- [ ] GDT/IDT implementation
- [ ] Interrupt handlers

### 📋 Planned
- [ ] Virtual memory and paging
- [ ] Multiprocessing support
- [ ] Filesystem (FAT12)
- [ ] Shell and user programs
- [ ] Network support (optional)

---

## 🏗️ System Architecture Overview

```
┌─────────────────────────────────────┐
│      User Programs (User Space)     │  Ring 3
│  Shell | App1 | App2 | ...          │
├─────────────────────────────────────┤
│          System Call Interface       │
├─────────────────────────────────────┤
│         Kernel (Kernel Space)        │  Ring 0
│  Process | Memory | File | Driver    │
├─────────────────────────────────────┤
│      Interrupt/Exception Handler     │
├─────────────────────────────────────┤
│       Hardware Abstraction Layer     │
├─────────────────────────────────────┤
│  CPU | Memory | Disk | Keyboard ... │  Hardware
└─────────────────────────────────────┘
```

---

## 📖 Important Concepts

### Real Mode vs Protected Mode

| Feature | Real Mode | Protected Mode |
|---------|-----------|----------------|
| Address Space | 1 MB (20-bit) | 4 GB (32-bit) |
| Memory Protection | ❌ No | ✅ Yes |
| Privilege Levels | Ring 0 only | Ring 0-3 |
| Multitasking | ❌ Limited | ✅ Full |
| Interrupts | BIOS Interrupts | IDT |
| Virtual Memory | ❌ No | ✅ Yes |

### Segmentation and Offset

```
Real mode address calculation:
Physical Address = Segment Base × 16 + Offset
Example: Segment 0x7c0, Offset 0x00 = 0x7c0 * 16 + 0 = 0x7c00

Protected mode:
Uses segment descriptors and paging (more complex but safer)
```

### Interrupts and Exceptions

```
Exceptions - CPU-detected error conditions
├── Fault    - Recoverable error (e.g., page fault)
├── Trap     - Trap (e.g., breakpoint)
└── Abort    - Unrecoverable abort (e.g., double fault)

Interrupts - External device requests
├── Hardware Interrupts (IRQ)
│   ├── IRQ0  - Timer
│   ├── IRQ1  - Keyboard
│   ├── IRQ3  - Serial COM2
│   ├── IRQ4  - Serial COM1
│   └── ...
└── Software Interrupts (INT)
    ├── int 0x10 - BIOS Display
    ├── int 0x13 - BIOS Disk
    └── ...
```

### GDT and IDT

**GDT (Global Descriptor Table):**
- Used in protected mode
- Defines segment attributes (base, limit, permissions)
- 8 bytes per descriptor
- Up to 8192 entries

**IDT (Interrupt Descriptor Table):**
- Similar to GDT, but for interrupt handlers
- Stores interrupt handler entry addresses
- 8 bytes per entry
- Up to 256 interrupts

---

## 🛠️ Development Guide

### Code Style

**Assembly Code (NASM syntax):**
```assembly
; Function example
; Parameters: EAX = value1, EBX = value2
; Return: EAX = result
global my_function
my_function:
    push rbp                ; Save old frame pointer
    mov rbp, rsp            ; Create new frame
    
    ; Function body
    add eax, ebx
    
    pop rbp                 ; Restore frame pointer
    ret
```

**C Code:**
```c
// Follow Linux kernel coding style
#include "stdio.h"

// Clear function names
int kernel_main(void) {
    printk("OS From Scratch\n");
    printk("Hello, World!\n");
    
    return 0;
}
```

### Commit Convention

Follow Conventional Commits format:

```bash
# Feature
git commit -m "feat: implement disk_load function"

# Fix
git commit -m "fix: resolve stack imbalance in bootloader"

# Documentation
git commit -m "docs: add bootloader documentation"

# Refactor
git commit -m "refactor: optimize memory allocator"

# Test
git commit -m "test: add unit tests for string functions"
```

### Branching Strategy

- `main` - Stable release
- `develop` - Development branch
- `feature/*` - Feature branches (e.g., `feature/bootloader`)
- `bugfix/*` - Bug fix branches (e.g., `bugfix/stack-issue`)

```bash
# Create feature branch
git checkout -b feature/bootloader

# Submit PR after development
git push origin feature/bootloader
```

---

## 🧪 Testing

### Build Test

```bash
# Check successful compilation
make clean
make
echo "Build successful"
```

### Runtime Test

```bash
# Test in QEMU
make qemu

# Automated testing
make test
```

### Debugging

```bash
# Debug with QEMU + GDB
make debug

# In gdb
(gdb) target remote localhost:1234
(gdb) file build/kernel.elf
(gdb) break main
(gdb) continue
```

---

## 📚 Reference Resources

### 📖 Recommended Books

1. **"Orange'S: An Operating System Implementation"** (in Chinese)
   - Most classic Chinese OS textbook
   - Detailed coverage of bootloader, protected mode, interrupts
   - Clear accompanying code

2. **"Operating Systems: Three Easy Pieces"**
   - Essential OS textbook in English
   - Focuses on principles and design
   - Both depth and breadth

3. **"The Linux Kernel Module Programming Guide"**
   - Linux kernel module programming
   - Practical and hands-on

### 🌐 Online Tutorials

- **[OSDev.org - OS Development Wiki](https://wiki.osdev.org/)**
  - Most comprehensive OS development reference
  - Covers x86, ARM, RISC-V architectures
  - Must-read resource

- **[Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/index.php)**
  - Classic kernel development tutorial
  - Beginner-friendly

- **[JamesM's Kernel Development Tutorials](https://www.jamesmolloy.co.uk/tutorial_html/)**
  - Detailed development steps
  - Easy-to-understand code

### 📘 Official Documentation

- **Intel x86-64 Architecture Manual**
  - Processor instructions and architecture
  - Authoritative reference

- **Intel Multiprocessor Specification**
  - Multiprocessor support

- **System V AMD64 ABI**
  - x86-64 calling conventions and ABI

### 🔧 Tools

- **NASM (Netwide Assembler)** - x86 assembler
- **GCC** - C compiler
- **QEMU** - Virtual machine emulator
- **GDB** - Debugger
- **Objdump** - Binary analysis tool

---

## 🤝 Contributing Guide

Pull Requests and Issues are welcome!

### Submitting Issues

1. Click "New Issue" on [Issues](../../issues) page
2. Choose appropriate template (Bug Report / Feature Request)
3. Clearly describe the issue or suggestion
4. Attach relevant code snippets, error messages, screenshots

**Issue Labels:**
- `bug` - Bug/defect
- `enhancement` - Improvement
- `documentation` - Documentation
- `help wanted` - Help needed
- `good first issue` - Good for beginners

### Submitting PRs

1. **Fork the repository**
   ```bash
   Click Fork button
   ```

2. **Clone your fork**
   ```bash
   git clone https://github.com/your-username/os-from-scratch.git
   cd os-from-scratch
   ```

3. **Create feature branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

4. **Commit changes**
   ```bash
   git add .
   git commit -m "feat: your feature description"
   git push origin feature/your-feature-name
   ```

5. **Submit Pull Request**
   - Click "Compare & pull request" on GitHub
   - Fill in clear title and description
   - Link related issues using `#issue_number`

**PR Checklist:**
- [ ] Code follows project style
- [ ] Added necessary comments
- [ ] Updated relevant documentation
- [ ] Commit messages are clear
- [ ] No warnings or errors introduced
- [ ] Tested locally

---

## 📄 License

This project is licensed under [MIT License](LICENSE).

```
MIT License

Copyright (c) 2026 Moyeswood

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction...
```

---

## 👨‍💻 About Author

**Moyeswood**

- GitHub: [@Moyeswood](https://github.com/Moyeswood)
- Email: li228459909@163.com

### Project Motivation

This project was born from curiosity about OS internals. By implementing each OS component step-by-step, I gradually understood how computers boot and run programs. I hope this project helps other learners gain deep system knowledge.

---

## 🎯 Project Value

After completing this project, you will be able to:

✅ Understand the computer boot process  
✅ Master x86 assembly language  
✅ Understand core OS design principles  
✅ Learn memory management and virtual memory  
✅ Understand interrupt and exception handling  
✅ Advance your systems programming skills  
✅ Build a solid foundation for studying Linux kernel  

---

## 🔗 Related Resources

- [Linux Kernel Source Code](https://github.com/torvalds/linux)
- [MINIX 3 - Educational Operating System](http://www.minix3.org/)
- [Pintos - Stanford OS Course](https://www.scs.stanford.edu/23sp-cs212/)

---

**⭐ If this project helps you, please give it a Star!**

**📧 Questions? Feel free to open an Issue or email me!**
