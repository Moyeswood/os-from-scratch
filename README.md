# 🚀 MyQwen-OS: A Minimalist 32-bit x86 Operating System

一个从零开始编写的 32 位 x86 架构微型操作系统内核。本项目旨在深入理解操作系统底层原理，涵盖引导、内存管理、中断处理、设备驱动及多任务调度。

## ✨ 核心特性

- **引导与保护模式**：自定义 Bootloader，完成实模式到 32 位保护模式的平滑切换。
- **实模式往返**：需要时临时降级到实模式调用 BIOS 服务（如 INT 10h 切换视频模式），用完自动升回保护模式。
- **内存管理**：基于位图（Bitmap）的物理内存分配器，支持页级分配。
- **虚拟内存**：实现分页机制（Paging），建立内核空间的恒等映射（Identity Mapping）。
- **中断系统**：完整的 IDT 初始化，8259A PIC 重映射，支持硬件中断处理。
- **设备驱动**：
  - VGA 文本模式显示驱动。
  - 串口（COM1）调试输出。
  - PS/2 键盘中断驱动。
  - PIT 定时器（IRQ0，约 18.2 Hz 滴答）+ PC 扬声器蜂鸣。
  - 模式 13h（320×200×256 色）图形库：调色板/画点/矩形/精灵/8x8 点阵文字。
- **交互接口**：内置轻量级 Shell，支持基础命令解析与输入历史。
- **多任务调度**：协作式（yield）+ **抢占式**（PIT IRQ0 时间片轮转）多任务，统一 44 字节上下文帧格式，两种切换路径产出的帧可互相恢复。
- **🎮 俄罗斯方块**：`tetris` 命令。从 1435 行实模式汇编版 1:1 移植的 C 版（7-bag 随机、wall kick、锁定延迟、暂存、消行闪烁动画、5 套主题配色、蜂鸣音效），ESC 退出回 Shell。
- **调试支持**：完美适配 QEMU + GDB 源码级远程调试。

## 📂 目录结构

```text
.
├── boot/
│   └── boot.asm          # 引导扇区代码 (512 Bytes)
├── kernel/
│   ├── entry.asm         # 内核入口
│   ├── kernel.c          # 内核主函数 (kernel_main)
│   ├── linker.ld         # 链接脚本，控制内存布局
│   ├── memory.c/h        # 物理内存位图管理
│   ├── paging.c/h        # 页目录与页表管理
│   ├── idt.c/h           # 中断描述符表管理
│   ├── pic.c/h           # 8259A 可编程中断控制器
│   ├── irq.asm           # 中断处理汇编桩 (IRQ Stubs)
│   ├── timer.c/h         # PIT 定时器 (滴答 + 蜂鸣)
│   ├── keyboard.c/h      # 键盘扫描码转 ASCII 驱动
│   ├── serial.c/h        # 串口调试输出
│   ├── vga.c/h           # VGA 显存操作
│   ├── graphics.c/h      # 模式 13h 图形库
│   ├── font8x8.h         # 8x8 点阵字体
│   ├── tetris.c/h        # 俄罗斯方块 (C 移植版)
│   ├── shell.c/h         # 命令行交互接口
│   ├── task.c/h          # 多任务管理与上下文初始化
│   ├── switch.asm        # 核心：寄存器压栈/出栈与上下文切换
│   ├── realmode.c/h      # 实模式跳板装载器 + 接口
│   ├── rm_entry.asm      # 32 位外壳: rm_switch_mode(mode)
│   ├── rm_trampoline.asm # 实模式跳板 (INT 10h 执行者)
│   └── rm_embed.asm      # 把跳板二进制嵌入内核镜像
├── tetris/               # 原始 1435 行实模式汇编版 (移植蓝本)
├── note/                 # 开发记录
├── Makefile              # 自动化编译与运行脚本
└── README.md             # 项目说明文档
```

## 🛠️ 编译与运行

1. 环境依赖

确保你的系统已安装以下工具：

- nasm (汇编器)
- gcc-multilib / i686-elf-gcc (32 位 C 编译器)
- ld (链接器)
- qemu-system-i386 (模拟器)
- gdb (可选，用于源码级调试)

2. 编译系统

```
make clean
make
```

3. 运行系统

```
# 正常运行
make run

# GDB 调试模式 (需打开两个终端)
# 终端 1: make debug
# 终端 2: gdb build/kernel.elf -> target remote localhost:1234
```

## 🎯 当前状态与下一步计划

- [x] 基础引导与保护模式
- [x] 物理与虚拟内存管理
- [x] 中断系统与键盘驱动
- [x] 协作式多任务 (Cooperative Multitasking)
- [x] 抢占式多任务 (Preemptive Multitasking with PIT)
- [x] 实模式往返 (BIOS INT 10h 视频模式切换)
- [x] 图形库 + PIT 定时器
- [x] 俄罗斯方块 (tetris 命令)
- [ ] 用户态与内核态隔离 (Ring 3)
- [ ] 简单的文件系统 (VFS)
- [ ] 超级马里奥 (复用 graphics.c 图形库)

## 📜 许可证

本项目仅供学习与研究使用。
