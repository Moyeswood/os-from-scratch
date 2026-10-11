<p align="center">
  <img src="assets/os-homepage-avatar.svg" width="220" alt="OS From Scratch Logo" />
</p>

# OS From Scratch 🖥️

A personal, hands-on journey to understanding how an operating system is built from the ground up.

[中文](#中文) | [English](#english)

---

# 中文

## 项目简介

这个项目从最底层开始学习操作系统：

- BIOS 和启动流程
- 引导加载器（Bootloader）
- x86 实模式与保护模式
- GDT / IDT / 中断管理
- 分页、虚拟内存与进程管理
- 汇编与 C 语言的真实协作

目标不是“做出一个成熟商业 OS”，而是通过实践理解计算机底层设计，以及操作系统为什么会以这种方式被构建出来。

---

## 为什么做这个项目

我对这些问题一直很感兴趣：

- 计算机为什么会启动？
- BIOS 是怎么找到启动磁盘的？
- 为什么 CPU 必须从实模式切换到保护模式？
- 为什么中断、分页和进程调度如此重要？

我发现，理解这些底层机制最好的办法，不是只看书，而是从最小的系统代码一步一步写起来。这个项目就是这样一个真实的学习过程：从最小引导程序出发，逐步认识操作系统的结构和设计思想。

---

## 学习路线

```text
Bootloader
  ↓
Real Mode
  ↓
Protected Mode
  ↓
Interrupts / Exceptions
  ↓
Memory Management
  ↓
Paging / Virtual Memory
  ↓
Processes / Scheduling
  ↓
Drivers / I/O
```

每一步都尽量保持“可理解、可调试、可扩展”的思路，而不是一开始就盯着庞大的内核代码。

---

## 当前状态

项目目前处于起步阶段，重点在于：

- 理解 BIOS 与 MBR 启动流程
- 学习 x86 实模式与内存布局
- 实现最小 bootloader
- 为后续保护模式、中断处理和内存管理打基础

这是一个“从 0 到 1”的底层学习旅程，而不是追求一开始就完美实现所有功能。

---

## 文档目录

- `docs/PROJECT_OVERVIEW.md`：技术博客风格的项目介绍
- `docs/LEARNING_JOURNEY.md`：个人成长日志与里程碑记录
- `docs/README_PERSONAL_JOURNEY.md`：更偏个人学习记录版

---

## 关键价值

这个项目的意义不只是写出一套代码，而是：

- 把抽象概念落到真实代码
- 理解底层运行原理
- 体验汇编、C、链接、调试和系统设计的结合
- 形成“从硬件到软件”的整体认知

---

## 联系方式

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

## 目标

我希望这个项目成为一段真实的技术成长记录：

- 从启动代码理解系统结构
- 从汇编与硬件细节理解 OS 设计
- 从代码实践建立底层系统编程认知

这是一个慢而扎实的学习过程，目标是理解，而不是追求展示。

---

# English

## Project Overview

This project is a hands-on journey to learning operating systems from the lowest level:

- BIOS and boot process
- bootloader design
- x86 real mode and protected mode
- GDT / IDT / interrupt handling
- paging, virtual memory, and process management
- real collaboration between assembly and C

The goal is not to build a perfect commercial operating system, but to understand how computers and operating systems are designed from the ground up through practical implementation.

---

## Why This Project

I have been curious about questions like:

- Why does a computer boot when powered on?
- How does BIOS find the boot device?
- Why must the CPU switch from real mode to protected mode?
- Why are interrupts, paging, and process scheduling so crucial?

I realized the best way to understand these low-level mechanisms is not to memorize theory, but to write the smallest possible system code and build upward from there.

This project records that learning process: starting from a minimal bootloader and gradually understanding the structure and logic of an operating system.

---

## Learning Roadmap

```text
Bootloader
  ↓
Real Mode
  ↓
Protected Mode
  ↓
Interrupts / Exceptions
  ↓
Memory Management
  ↓
Paging / Virtual Memory
  ↓
Processes / Scheduling
  ↓
Drivers / I/O
```

I want each stage to be understandable, debuggable, and extensible instead of jumping straight into a huge kernel.

---

## Current Status

The project is in its early stage, focusing on:

- understanding BIOS and MBR startup flow
- learning x86 real mode and memory layout
- implementing a minimal bootloader
- preparing for protected mode, interrupts, and memory management

This is a gradual journey from 0 to 1, rather than chasing full completion too early.

---

## Documentation

- `docs/PROJECT_OVERVIEW.md` — blog-style project overview
- `docs/LEARNING_JOURNEY.md` — personal learning log and milestone tracking
- `docs/README_PERSONAL_JOURNEY.md` — a more personal learning-record version

---

## Core Value

The real value of this project is not only writing code. It is:

- turning abstract concepts into real code
- understanding the low-level behavior of computers
- experiencing the combined use of assembly, C, linking, and debugging
- forming a complete picture from hardware to software

---

## Contact

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

## Goal

I want this project to serve as a real technical growth record:

- understanding system structure from startup code
- understanding OS design through assembly and hardware details
- building a practical understanding of system programming from code

This is a slow but meaningful learning path, and the focus is understanding, not showing off.

---

<p align="center">
  <sub>Built from curiosity, learned through code, and documented step by step.</sub>
</p>
