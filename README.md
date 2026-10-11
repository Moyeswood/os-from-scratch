# OS From Scratch 🖥️

A personal journey of learning operating systems from the ground up.

[中文](#中文) | [English](#english)

---

# 中文

## 我的学习故事

我从一开始就对底层系统感到好奇：

- 为什么计算机会从开机启动？
- 为什么 BIOS 能找到启动盘？
- 为什么处理器会从实模式进入保护模式？
- 为什么操作系统必须管理内存、中断、进程？

这些问题一直困扰着我。我意识到，真正理解计算机，不只是会写应用，而是要从最底层开始理解它的运行方式。

于是，我决定做一个项目：

> 从零开始实现一个最小的操作系统，逐步学习汇编、C 语言、编译链、底层硬件和操作系统设计。

这个项目不是为了做一个商业级操作系统，而是为了追踪一个真正的学习过程：

- 从最小启动代码到内核
- 从 BIOS 到保护模式
- 从汇编到 C 语言
- 从概念理解到实际代码实现

---

## 这个项目的意义

我想通过这个项目真正理解：

- 计算机启动的全过程
- 引导加载器为什么如此重要
- x86 汇编语言如何控制硬件
- 中断和异常在系统中的作用
- 虚拟内存、分页和地址转换
- 为什么操作系统必须管理进程和内存

更重要的是，我希望它能记录我的成长，而不是只停留在最终代码层面。

---

## 学习里程碑

### 阶段一：建立认识（已完成）

- 建立 GitHub 仓库
- 完成项目结构和文档配置
- 理解操作系统学习路线
- 明确目标：从 Bootloader 到 Kernel

**收获：**
- 明确了学习目标
- 认识到底层学习需要从最小构件开始
- 明白项目不是追求巨大复杂度，而是追求可理解性

---

### 阶段二：启动流程的理解（进行中）

**目标：**
- 理解 BIOS 启动过程
- 学习实模式与 16 位代码
- 用汇编实现 MBR 引导程序
- 理解磁盘读取中断 int 0x13

**关键问题：**
- 为什么第一个扇区必须是 512 字节？
- 为什么 bootloader 被加载在 0x7c00？
- 为什么要检查 0xAA55 引导标志？

**当前焦点：**
- 让最小 bootloader 可以从磁盘读取下一段代码
- 理解引导过程中的寄存器和内存布局

---

### 阶段三：保护模式（计划中）

**目标：**
- 理解 GDT（全局描述符表）
- 学习段描述符和权限级别
- 实现从实模式到保护模式的切换
- 进入 32 位代码执行环境

**关键学习点：**
- 为什么 CPU 需要保护模式
- 为什么内存访问需要权限控制
- 为什么现代 OS 不能长期停留在实模式

---

### 阶段四：中断与异常（计划中）

**目标：**
- 理解 IDT（中断描述符表）
- 学习中断处理器设计
- 处理键盘、时钟和系统异常
- 理解事件驱动型系统设计

**关键学习点：**
- 中断和函数调用的相似之处
- 硬件事件如何触发系统响应
- 为什么操作系统必须管理异常与优先级

---

### 阶段五：内存管理与分页（计划中）

**目标：**
- 学习虚拟内存
- 理解分页和页表
- 理解地址翻译
- 进行地址隔离和内存保护

**关键学习点：**
- 虚拟地址和物理地址的区别
- 页目录与页表的机制
- 为什么分页是现代 OS 的核心能力之一

---

### 阶段六：进程与调度（计划中）

**目标：**
- 理解 PCB
- 学习上下文切换
- 设计一个简单调度器
- 体会多任务系统的真实实现方式

**关键学习点：**
- 进程隔离与共享
- CPU 调度为什么是系统设计的核心
- 为什么上下文切换如此重要

---

## 我在学习中的成长

### 从“看概念”到“看代码”

刚开始的时候，我只知道这些词：

- 操作系统
- 中断
- 虚拟内存
- 进程
- 分页

但它们在我的脑子里还是非常抽象的。后来开始写启动代码后，一切都变得更具体了：

- BIOS 是怎么找启动盘的
- 程序是如何加载到内存的
- CPU 是如何切换模式的
- 为什么中断是系统的核心机制之一

这时候，我不再只是“背概念”，而是在理解真正的工作原理。

---

### 从“知道它存在”到“知道它为什么存在”

我最重要的一点成长，是把学习从“知道它是什么”变成了“知道它为什么存在”。

例如：

- 为什么需要保护模式？
- 为什么要分页？
- 为什么中断不能只是普通函数调用？
- 为什么系统需要进程隔离？

这些问题的答案，构成了我对操作系统真正的理解。

---

## 当前学习状态

目前我正处于以下阶段：

- 基础项目结构已建立
- 文档和学习记录已整理
- 正在研究 x86 启动流程与 MBR
- 计划下一步写最小 bootloader
- 目标是最终写出��运行的最小内核雏形

这是一段慢工出细活的过程，但也正因为如此，学到的东西会更牢固。

---

## 技术路线图

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

这条路线我会逐步走完，一步一步把底层系统构建起来。

---

## 我的目标

这个项目不是为了追求完整成熟的操作系统，而是为了：

- 真正理解系统运行原理
- 用实践感受汇编、C 和编译链接的真实运用
- 把 OS 相关知识从理论变成代码
- 形成自己的底层系统编程理解框架

---

## 联系方式

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

**这是一个从 0 到 1 的底层学习项目。**

我希望用代码去理解计算机，而不是仅仅看它运行。 

---

# English

## My Learning Story

I have always been curious about the low-level world of computers:

- Why does a computer boot when powered on?
- How does BIOS find the boot device?
- Why does the CPU switch from real mode to protected mode?
- Why does the OS manage memory, interrupts, and processes?

These questions kept pushing me to go deeper. I realized that understanding computers is not only about writing applications, but about understanding what happens under the hood.

So I started this project:

> Build a minimal operating system from scratch, step by step, learning assembly, C, linker behavior, low-level hardware, and operating system design.

This is not a project to build a commercial OS. It is a project to record a real learning journey:

- from bootloader to kernel
- from BIOS to protected mode
- from assembly to C language
- from abstract concepts to executable code

---

## Why This Project Matters

I want to truly understand:

- how the computer boots
- how a bootloader works
- how x86 assembly controls the CPU and hardware
- how interrupts and exceptions drive the system
- how paging and virtual memory work
- why process and memory management are essential in an OS

More than anything, I want this project to reflect my growth, not just a final codebase.

---

## Learning Milestones

### Phase One: Building Awareness (Completed)

- Created GitHub repository
- Established project structure and documentation
- Defined the learning roadmap
- Clarified the goal: from bootloader to kernel

**What I gained:**
- Clear learning direction
- Realized that low-level learning starts from the smallest building blocks
- Understood that project quality is not about complexity alone, but about clarity and understanding

---

### Phase Two: Startup Process (In Progress)

**Goal:**
- understand BIOS startup
- learn real mode and 16-bit execution
- implement a minimal MBR bootloader
- understand int 0x13 disk read

**Key questions:**
- Why is the first sector 512 bytes?
- Why is the boot code loaded at 0x7c00?
- Why does the boot sector need the 0xAA55 signature?

**Current focus:**
- make a minimal bootloader read the next code sector from disk
- understand memory layout and register behavior during startup

---

### Phase Three: Protected Mode (Planned)

**Goal:**
- learn GDT (Global Descriptor Table)
- understand segment descriptors and privilege levels
- implement the switch from real mode to protected mode
- enter 32-bit execution

**Key learning points:**
- why protected mode is necessary
- why memory access needs privilege control
- why modern OSes cannot stay in real mode forever

---

### Phase Four: Interrupts and Exceptions (Planned)

**Goal:**
- understand IDT (Interrupt Descriptor Table)
- study interrupt handlers
- handle keyboard, timer, and system exceptions
- understand event-driven system design

**Key learning points:**
- similarity between interrupts and function calls
- how hardware events trigger software actions
- why the OS must manage exceptions and priorities

---

### Phase Five: Memory Management and Paging (Planned)

**Goal:**
- study virtual memory
- understand paging and page tables
- understand address translation
- implement memory protection and isolation

**Key learning points:**
- difference between virtual and physical addresses
- page directory and page table mechanism
- why paging is one of the core features of modern operating systems

---

### Phase Six: Processes and Scheduling (Planned)

**Goal:**
- understand PCB
- learn context switching
- build a basic scheduler
- experience the real logic of multitasking

**Key learning points:**
- process isolation and sharing
- why scheduling is central to OS design
- why context switching matters so much

---

## My Growth During the Learning Process

### From "knowing the concept" to "seeing the code"

At first, I only knew the words:

- OS
- interrupt
- memory
- page table
- process

They were still very abstract. After I started working on startup code, everything became more concrete:

- how BIOS finds the boot device
- how code is loaded into memory
- how the CPU changes modes
- why interrupts are a core system mechanism

This changed my learning from memorization to understanding.

---

### From "I know it exists" to "I know why it exists"

One of the biggest changes was moving from "I know what it is" to "I know why it's designed this way."

Examples:

- Why do we need protected mode?
- Why do we need paging?
- Why are interrupts not just normal function calls?
- Why must the OS isolate processes?

These questions become the core of my understanding.

---

## Current Learning Status

Right now, I am at the following stage:

- project structure is established
- documentation and learning records are in place
- studying the x86 startup flow and MBR
- preparing to write a minimal bootloader
- aiming to build a working minimal kernel prototype

This is a slow and deliberate process, but it is the right way to learn deeply.

---

## Technical Roadmap

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

I will work through this path step by step and build the system piece by piece.

---

## My Goal

This project is not about building a massive production OS. It is about:

- understanding system internals deeply
- applying assembly, C, and linkers in a real context
- turning OS theory into executable code
- building a practical understanding of how computers work at the lowest level

---

## Contact

- GitHub: https://github.com/Moyeswood
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

**This is a project of learning from 0 to 1.**

I want to understand computers by writing code, not just by reading about them.

---
