# OS From Scratch 🖥️

A hands-on operating system learning project from bootloader to kernel.

[中文](#中文) | [English](#english)

---

# 中文

## 项目简介

这是一个从零开始构建微型操作系统的学习项目，目标是通过实践理解底层计算机原理。

我希望通过这个项目真正理解：

- 计算机是如何启动的
- BIOS 和引导加载器如何工作
- 处理器如何从实模式切换到保护模式
- 中断和异常如何驱动系统
- 内存管理、分页与虚拟内存的原理
- 汇编、C 语言和系统编程如何协同工作

这是一个偏“底层系统编程 + 实战探索”的项目，不是拿来炫技，而是用来真正理解计算机从0到1运行的方式。

---

## 为什么做这个项目

很多人学操作系统时，只停留在概念层面：进程、线程、内存、调度、分页、信号、中断……

但真正理解这些抽象，最好的方式不是只看书，而是从最底层开始构建一个最小系统。

这个项目的核心价值在于：

- 把抽象概念落到真实代码上
- 让引导程序、内核、内存、异常处理像真实系统一样串联起来
- 用汇编和 C 语言把底层能力真正跑起来
- 在调试中理解为什么系统必须这样设计

---

## 学习路线

- Bootloader：理解 BIOS、MBR、磁盘读取和加载流程
- Real Mode：理解 x86 实模式与内存布局
- Protected Mode：学习 GDT、段描述符和模式切换
- Interrupts：理解 IDT、中断、异常和 IRQ
- Memory Management：实现分页和虚拟内存
- Processes：理解上下文切换和进程调度
- Drivers：探索磁盘、键盘、串行设备等硬件接口

---

## 当前状态

当前项目正处于起步阶段，重点在于：

- 研究 x86 启动流程
- 理解 MBR 与引导扇区
- 实现最小可启动引导程序
- 为后续内核、保护模式和中断处理打基础

---

## 目录结构

```text
os-from-scratch/
├── README.md
├── docs/
│   ├── PROJECT_OVERVIEW.md
│   ├── LEARNING_JOURNEY.md
│   └── ...
├── bootloader/
├── kernel/
├── arch/
├── lib/
├── tools/
└── ...
```

---

## 技术栈

- Assembly / NASM
- C language
- GCC / ld
- QEMU
- GDB
- Linux / x86 architecture

---

## 快速开始

```bash
git clone https://github.com/Moyeswood/os-from-scratch.git
cd os-from-scratch

# 构建和运行（根据后续开发进度调整）
make
make qemu
```

---

## 项目文档

- `docs/PROJECT_OVERVIEW.md`：技术博客风格的项目说明
- `docs/LEARNING_JOURNEY.md`：个人成长日志和学习里程碑记录

---

## 联系方式

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

## 目标

这个项目的目标不是“做一个成熟的商业 OS”，而是：

- 真正理解底层系统工作原理
- 学习汇编、C 语言和编译链接的真实应用
- 建立对操作系统设计的直接体验
- 把操作系统原理从书本知识变成代码实践

---

**如果你也对底层系统感兴趣，欢迎一起学习。**

---

# English

## Project Overview

This is a hands-on project for building a minimal operating system from scratch, with the goal of understanding low-level computer architecture through practical implementation.

The aim is to truly understand:

- how a computer starts up
- how BIOS and bootloaders work
- how the CPU transitions from real mode to protected mode
- how interrupts and exceptions drive the system
- how memory management, paging, and virtual memory work
- how assembly, C, and system programming work together

This is a project focused on low-level systems programming and practical exploration, not just theory.

---

## Why This Project

Many people study operating systems only at the conceptual level: processes, threads, memory, scheduling, paging, signals, interrupts, and so on.

But the best way to understand these abstractions is not simply to read books—it is to build a minimal system from the ground up.

The value of this project is that it:

- turns abstract ideas into real code
- connects the bootloader, kernel, memory, and exception handling into one working system
- makes assembly and C language genuinely useful in a real system context
- helps reveal why the OS must be designed in a particular way

---

## Learning Roadmap

- Bootloader: understand BIOS, MBR, disk reading, and boot flow
- Real Mode: understand x86 real mode and memory layout
- Protected Mode: learn GDT, segment descriptors, and mode switching
- Interrupts: understand IDT, interrupts, exceptions, and IRQ
- Memory Management: implement paging and virtual memory
- Processes: understand context switching and scheduling
- Drivers: explore hardware interfaces like disks, keyboard, and serial devices

---

## Current Status

This project is in its early stage, with focus on:

- studying the x86 startup process
- understanding MBR and boot sectors
- implementing a minimal bootloader
- preparing the foundation for the kernel, protected mode, and interrupts

---

## Repository Structure

```text
os-from-scratch/
├── README.md
├── docs/
│   ├── PROJECT_OVERVIEW.md
│   ├── LEARNING_JOURNEY.md
│   └── ...
├── bootloader/
├── kernel/
├── arch/
├── lib/
├── tools/
└── ...
```

---

## Tech Stack

- Assembly / NASM
- C language
- GCC / ld
- QEMU
- GDB
- Linux / x86 architecture

---

## Quick Start

```bash
git clone https://github.com/Moyeswood/os-from-scratch.git
cd os-from-scratch

# Build and run (adjust according to ongoing development stage)
make
make qemu
```

---

## Project Documentation

- `docs/PROJECT_OVERVIEW.md`: blog-style project overview
- `docs/LEARNING_JOURNEY.md`: personal growth log and milestone tracking

---

## Contact

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

## Goal

This project is not about building a production-ready OS. The real goal is to:

- truly understand system internals
- learn how assembly, C, and linker tools are used in real systems
- experience OS design firsthand
- turn OS theory into working code

---

**If you're also interested in low-level systems, feel free to follow along.**
