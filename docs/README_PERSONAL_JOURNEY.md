# OS From Scratch 🖥️

A personal journey of learning how an operating system is built from the ground up.

[中文](#中文) | [English](#english)

---

# 中文

## 从“看概念”到“看代码”

我一直很好奇底层系统是怎么工作的：

- 为什么计算机开机后会启动？
- BIOS 是怎么找到启动盘的？
- 为什么操作系统需要切换到保护模式？
- 中断、异常、分页、虚拟内存、进程调度，它们又是怎样串起来的？

我发现，真正理解这些内容，最好的方式并不是只看书，而是从最底层开始写代码。

所以，我开始了这个项目：

> 从零开始构建一个最小操作系统，用汇编、C 语言和底层知识理解计算机是如何从启动到运行程序的。

这不是为了做一个完美的商业操作系统，而是为了把那些抽象的底层概念变成真实、可运行、可调试的代码。

---

## 我的学习动机

最初，我只是想理解这些问题：

- 计算机为什���能开机？
- 程序为什么能运行？
- 为什么 OS 必须管理内存和 CPU？
- 为什么中断和分页如此重要？

这些问题都指向一个核心事实：

所有应用软件的运行，最终都建立在底层硬件和操作系统的抽象之上。

如果不理解这些底层机制，就很难真正理解程序、系统和计算机的本质。

---

## 这条路我想走的是什么

我希望这个项目成为一个真实的“底层学习记录”：

- 从 BIOS 和引导程序出发
- 进入 x86 实模式
- 学习保护模式和 GDT
- 理解 IDT 和中断处理
- 理解分页与虚拟内存
- 再一步步走向进程管理和调度

我不追求一开始就做出一个完整的系统，我更关心的是：

- 这个东西为什么要这样设计
- 代码为什么要这样写
- 底层抽象是如何从硬件上升到软件的

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

这是一条慢而扎实的路径。我希望每走一步，都能真正理解它，而不只是“知道有这么个概念”。

---

## 当前进度

目前项目处于起步阶段，重点在于：

- 理解 BIOS 启动流程
- 学习 x86 实模式和 MBR
- 实现最小 bootloader
- 为后续保护模式和中断处理打基础

我不是匆忙追求完成，而是希望在每一阶段都真正建立理解。

---

## 我在这个项目中得到的东西

这个项目对我来说不只是代码，它更像是一段技术成长记录：

- 看到了计算机启动的真实过程
- 感受到了汇编和低层编程的严谨性
- 认识到系统设计不是“加功能”，而是“解决约束”
- 逐渐形成“从硬件到软件”的整体理解

最重要的，是在做这些东西时，我慢慢明白：

软件并不是凭空站着的，它是建立在 CPU、内存、总线和中断这些底层机制之上的。

---

## 项目文档

这个仓库里我已经整理了一些材料：

- `docs/PROJECT_OVERVIEW.md`：更偏技术博客风的项目介绍
- `docs/LEARNING_JOURNEY.md`：个人成长日志和学习里程碑
- `docs/README_PERSONAL_JOURNEY.md`：更偏个人学习记录的版本

这些文档记录的是我学习的思路、阶段性收获和成长过程。

---

## 我的目标

我并不只是想“写出一个 OS”，而是想建立一套对计算机底层的理解框架：

- 计算机如何启动
- 程序如何加载和执行
- 内存和 CPU 如何协作
- 中断、调度和虚拟内存如何工作
- 为什么操作系统是现代计算机的基石

这是一个属于“从 0 到 1”的学习过程，也是我持续成长的技术记录。

---

## 联系方式

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

**这是我在底层系统学习中的一段真实记录。**

从启动代码到内核设计，从汇编到系统编程，我希望一步一步把它看清楚。 

---

# English

## From “knowing the concept” to “seeing the code”

I have always been curious about how low-level systems work:

- Why does a computer boot when powered on?
- How does BIOS find the boot drive?
- Why does the OS switch to protected mode?
- How do interrupts, exceptions, paging, virtual memory, and process scheduling connect together?

I realized the best way to understand these topics is not just by reading textbooks, but by writing code from the lowest level.

So I started this project:

> Build a minimal operating system from scratch, learning assembly, C, low-level hardware, and OS design step by step.

This is not about creating a perfect production OS. It is about turning abstract low-level concepts into real, runnable, debuggable code.

---

## My Learning Motivation

At first, I only had a few questions:

- How does a computer start?
- How does a program run?
- Why does the OS manage memory and CPU?
- Why are interrupts and paging so important?

These questions all point to one fact:

Every software program runs on top of hardware and operating system abstractions.

If you do not understand those foundations, it is hard to understand the true nature of software and computing.

---

## What I Want to Explore

I want this project to be a real record of low-level learning:

- begin with BIOS and the bootloader
- move into x86 real mode
- study protected mode and GDT
- understand IDT and interrupt handling
- learn paging and virtual memory
- then move toward process management and scheduling

I am not chasing a perfect kernel from the start. I care more about this:

- why the system is designed this way
- why the code is written this way
- how low-level abstractions emerge from hardware

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

This is a slow and deliberate path. I want to understand each step deeply, not just memorize concepts.

---

## Current Progress

The project is still in its early stage. My focus right now is:

- understanding BIOS startup flow
- learning x86 real mode and MBR
- implementing a minimal bootloader
- preparing for protected mode and interrupt handling

I am not rushing to finish. I want each stage to build real understanding.

---

## What This Project Gives Me

For me, this project is not just code—it is a record of technical growth:

- I can see the actual boot process of a computer
- I feel the rigor of assembly and low-level programming
- I realize system design is not about adding features, but about resolving constraints
- I begin to see the whole picture from hardware to software

Most importantly, I am learning this:

Software does not appear out of nowhere. It rests on CPU, memory, buses, and interrupt mechanisms.

---

## Project Documentation

This repository includes several references:

- `docs/PROJECT_OVERVIEW.md`: a blog-style overview of the project
- `docs/LEARNING_JOURNEY.md`: personal learning log and milestones
- `docs/README_PERSONAL_JOURNEY.md`: a more personal growth-oriented version

These documents capture my thought process, learning milestones, and the evolution of my understanding.

---

## My Goal

I do not want to simply “build an OS.” I want to build a framework for understanding the low-level foundations of computers:

- how the system boots
- how programs are loaded and executed
- how memory and CPU cooperate
- how interrupts, paging, and virtual memory work
- why operating systems are the foundation of modern computing

This is a journey from 0 to 1, and it is also a technical record of my growth.

---

## Contact

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

**This is a real record of my low-level systems learning journey.**

From startup code to kernel design, from assembly to system programming, I want to understand it one step at a time.
