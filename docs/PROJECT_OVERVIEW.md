# OS From Scratch - Project Overview

[中文](#中文) | [English](#english)

---

# 中文

## 一句话介绍

从零开始实现一个微型操作系统，通过实战深入理解计算机启动、内存管理、中断处理和系统编程的底层原理。

---

## 为什么要做这个项目？

很多人学操作系统时，看到的是大量概念：进程、线程、调度、分页、虚拟内存、中断……但真正把它们串起来时，往往只停留在“知道有这么回事”的层面。

而真正的理解，来自于动手做一个最小的系统。你不会从 Linux 内核那样庞大的代码开始，而是从最底层的启动过程入手：

- 从 BIOS 如何加载引导程序开始
- 如何从实模式进入保护模式
- 如何通过中断处理硬件事件
- 如何实现基本的内存管理
- 如何逐步构建一个真正能运行的内核

这就是“从零开始构建一个操作系统”的意义：不仅是学习概念，更是理解这些概念为什么会以这种方式被设计出来。

---

## 这个项目教给你什么？

### 1. 汇编语言的真实用法

你会接触 x86 汇编，理解寄存器、栈、函数调用、地址计算、BIOS 中断。它不是“纸上谈兵”的汇编，而是直接在启动阶段、内核入口、低层中断处理里使用。

### 2. 计算机系统的底层视角

你会理解：
- 计算机为什么要从实模式启动
- 为什么需要保护模式
- GDT 和 IDT 到底是干什么的
- 为什么中断处理会改变 CPU 的执行流
- 为什么内存管理要分段、分页和地址转换

### 3. 编译原理中的真实场景

从链接脚本、地址重定位、段布局到二进制镜像生成，你会在工程中真实感受到：
- 编译器生成的二进制不是直接执行的
- 加载器和链接器负责把代码、数据和栈组织起来
- 操作系统不是“从代码中凭空出现”的，而是有明确的加载和布局过程

### 4. 调试和定位 bug 的能力

做系统开发最大的挑战，不是“从零写出代码”，而是“为什么它崩了”。QEMU 和 GDB 会让你学会：
- 观察寄存器
- 查看内存布局
- 追踪中断流
- 对照指令级执行过程定位错误

---

## 这个项目与传统学习方式的区别

### 传统学习方式

很多人通常是：
- 看理论课件
- 记住 OS 原理概念
- 看别人的代码
- 但不知道为什么这是如此实现

这时候，你能说出“分页是干什么的”，但写不出一个简单的页表映射。

### 实战学习方式

这个项目的方式是：
- 从最小可启动代码开始
- 用汇编写 MBR 和引导程序
- 逐步实现 GDT、加载内核、切换保护模式
- 加入中断处理器和驱动程序
- 最后把一套独立运行的系统拼起来

你学到的不是“概念背诵”，而是“系统是怎么工作的”。

---

## 学习路线：从最小启动到完整内核

### 第 1 阶段：引导加载器（Bootloader）

目标：理解计算机启动流程，并把内核从磁盘读入内存。

关键问题：
- BIOS 是如何找到启动盘的？
- 为什么第一个扇区必须有 0xAA55 结束标志？
- 为什么引导代码通常在 0x7c00 处加载？
- 磁盘读取函数 int 0x13 到底做了什么？

这一步是整个项目的起点。很多人第一次看到代码，如此短小的汇编，却能完成磁盘读取和跳转。这种体验非常有“底层开发”的感觉。

### 第 2 阶段：进入保护模式

目标：从实模式切换到保护模式，从有限的 1MB 空间迈向真正的系统编程环境。

关键点：
- GDT（全局描述符表）
- 段描述符与权限级别
- CR0 寄存器中的 PE 位
- 32 位代码入口

你会意识到：操作系统不是单纯“启动起来”，而是需要切换到一个更安全、更强大的 CPU 模式。

### 第 3 阶段：中断与异常

目标：理解中断是如何驱动系统工作的。

关键点：
- 中断和异常的区别
- IDT（中断描述符表）
- IRQ 与硬件事件
- 键盘、时钟、磁盘事件的处理方式

这一阶段常常让人有一个明显的顿悟：
原来中断就是硬件和软件之间的“通知机制”，而整个系统就建立在这种机制上。

### 第 4 阶段：内存管理

目标：理解程序如何在内存中真正运行。

关键点：
- 虚拟地址和物理地址
- 页表和分页机制
- 缺页异常
- 堆和栈的布局

这一步会让你理解：为什么操作系统必须把“看似无限的内存”抽象成一个安全、可控的地址空间。

### 第 5 阶段：进程与调度

目标：让系统变成真正的多任务环境。

关键点：
- PCB（进程控制块）
- 上下文切换
- 轮询调度和优先级调度
- 进程间通信

这时候你会开始真正理解“为什么多任务需要上下文切换”，而不再只是��留在概念描述上。

---

## 这个阶段中常见的“顿悟时刻”

### 顿悟 1：真正启动代码就这么短

你会发现一个最小的操作系统并不需要巨大的代码量，它从一个 512 字节的引导扇区开始，接着加载更大的代码，这就是整个系统最初的骨架。

### 顿悟 2：系统根本不是在“写程序”，而是在“组织硬件资源”

你会开始理解，操作系统真正做的事不是炫技，而是：
- 管理 CPU
- 管理内存
- 调度进程
- 响应中断
- 接管设备

### 顿悟 3：中断不是“功能”，而是“机制”

中断处理不是简单的函数调用，而是 CPU 的控制流被硬件强行切换。你会慢慢认识到，整个现代计算机中的异步事件处理，都是建立在这套机制上的。

### 顿悟 4：每一行代码都在服务底层设计

当你一步步写出 GDT、IDT、页表时，就会明白：很长的 OS 代码，并不是为了炫技，而是为了让 CPU 一步步在“安全、受控、可扩展”的条件下工作。

---

## 为什么这个项目适合我？

如果你具备以下兴趣或目标，这个项目尤其合适：

- 你想理解操作系统真正是怎么启动的
- 你想学汇编，并不是对着 hello world 练习，而是用于真实系统编程
- 你想理解计算机底层抽象和硬件交互
- 你想学系统编程、驱动、内存管理、调试
- 你想把“操作系统原理”这门课从概念变成实战

如果你只是想做一个前端项目、脚本工具或者简单的 Web 应用，那么这是一个不同方向的学习路径。但如果你想真正理解计算机底层，这个项目会带来非常强的成长。

---

## 项目价值：不仅是技术，更是思维方式

完成这个项目以后，你会发现自己对计算机的看法完全不同：

- 不再把程序看成“只在代码层面运行”
- 不再把内存看成“一个大数组”
- 不再把中断看成“一个语法概念”
- 不再把系统看成“黑盒”

你开始真正理解：一切软件的运行，最终都依赖于硬件如何工作、CPU 如何调度、内存如何组织、设备如何响应。

这就是系统编程的价值，也正是这个项目带给你的核心收获。

---

## 这个项目适合的时间投入

这个项目并不是一周就能完成的，它更像一场深度学习旅程。通常推荐：

- 1-2 周：Bootloader 和启动流程
- 1-2 周：保护模式和 GDT
- 1-2 周：中断和 IDT
- 2-3 周：内存管理和分页
- 2-3 周：进程、调度和驱动

总的来说，2 到 3 ���月的持续投入，才是比较合理的节奏。

---

## 结语

这个项目不只是一个“OS 教程”，它更像一次真正的底层编程探险：

从 0x7c00 开始，
从 BIOS 启动开始，
从汇编和寄存器开始，
一步步理解计算机系统如何工作。

真正的乐趣，不在于写出一个巨大的系统，而在于：
你在还不理解底层原理时，敢于从最小的代码开始；
你在每一次失败中学到新东西；
你最终看到自己的 OS 从黑屏走到能够打印出文字、处理中断、管理内存。 

这就是我做这个项目的理由。

---

# English

## One-line Introduction

Build a minimal operating system from scratch to deeply understand the low-level principles behind computer booting, memory management, interrupts, and system programming.

---

## Why Build This Project?

Many people learn operating systems by memorizing concepts such as processes, threads, scheduling, paging, virtual memory, and interrupts. But when asked, "How does an OS actually boot?" or "How does the CPU switch from real mode to protected mode?" the answer often becomes vague.

The real understanding comes from hands-on implementation.

Instead of starting from a massive Linux kernel codebase, this project begins at the lowest level:

- How BIOS loads the boot sector
- How the system transitions from real mode to protected mode
- How interrupts handle hardware events
- How basic memory management is structured
- How a simple kernel is built step by step

This is not just about learning theory; it is about understanding why the design exists in the first place.

---

## What This Project Teaches You

### 1. Real assembly programming

You will work with x86 assembly and learn registers, stack frames, function calls, memory addressing, and BIOS interrupt handling. This is not toy-level assembly—it's the kind used in real boot code and low-level system interfaces.

### 2. Low-level system thinking

You will understand:
- Why the system starts in real mode
- Why protected mode exists
- What GDT and IDT do
- Why interrupts alter CPU control flow
- Why memory must be segmented and paged

### 3. Compilation and linking in real use cases

From linker scripts and address relocation to binary image generation, you will experience the reality that:
- compiler output is not directly executable
- loaders and linkers organize code, data, and stack layout
- an OS is not magically assembled from source—there is a clear layout and loading process

### 4. Debugging skills that matter in systems work

A major challenge in OS development is not writing code but diagnosing why it fails. QEMU and GDB help you learn to:
- inspect registers
- check memory layout
- trace interrupt flow
- locate bugs by instruction-level execution

---

## Why This Is Different From Traditional Learning

### Traditional learning

Most people study OS by:
- reading theory notes
- memorizing concepts
- reading other people's code
- but not understanding why it is implemented that way

This leads to knowing the terms but not being able to build even a minimal memory mapping or bootloader.

### Project-based learning

This project teaches by doing:
- start from the smallest bootable code
- write the MBR and bootloader in assembly
- implement GDT and transition to protected mode
- add interrupt handling and drivers
- progressively build a small but real kernel

You are learning not just definitions, but how the system actually works.

---

## Learning Path: From Boot to Kernel

### Phase 1: Bootloader

Goal: understand how the machine starts and load the kernel from disk.

Key questions:
- How does BIOS find a bootable disk?
- Why does the first sector need the 0xAA55 signature?
- Why is the bootloader typically loaded at 0x7c00?
- What does int 0x13 actually do?

This is the starting point of the journey. Very often, the first time someone sees a tiny bootloader program that reads sectors and jumps to a new address, the understanding of the machine becomes much more concrete.

### Phase 2: Protected Mode

Goal: transition from real mode to protected mode and enter a more capable runtime environment.

Key ideas:
- GDT
- segment descriptors
- privilege levels
- CR0 protection enable bit
- 32-bit entry point

This is the moment when you realize the OS isn't just "starting up"—it is entering a new execution model with stronger memory control and process safety.

### Phase 3: Interrupts and Exceptions

Goal: understand how the system reacts to hardware and software events.

Key ideas:
- difference between interrupt and exception
- IDT
- IRQ handling
- keyboard and timer event processing

This stage often triggers a major realization: interrupts are the mechanism by which hardware communicates with software, and modern multitasking and event handling depend on this design.

### Phase 4: Memory Management

Goal: understand how programs run in a structured memory space.

Key ideas:
- virtual vs physical memory
- page tables and paging
- page faults
- stack and heap layout

This is where you understand why an OS must abstract memory into a safe, controlled address space.

### Phase 5: Processes and Scheduling

Goal: turn the system into a true multitasking environment.

Key ideas:
- PCB
- context switching
- scheduling algorithms
- IPC

At this point, you finally understand why multitasking involves saving and restoring CPU state in precise ways.

---

## Typical “Aha” Moments

### Aha 1: the bootloader is surprisingly small

You realize a minimal OS does not begin with thousands of lines of code. It begins with a tiny boot sector that reads a larger kernel from disk and jumps into it.

### Aha 2: the OS is not writing programs—it is organizing resources

You begin to see that an OS is responsible for:
- managing the CPU
- managing memory
- scheduling tasks
- responding to interrupts
- exposing devices to software

### Aha 3: interrupts are not just features—they are the system's communication protocol

You will see that interrupts are how hardware tells software that an event happened. This is foundational to asynchronous system design.

### Aha 4: every line of code exists to support the architecture

As you implement GDT, IDT, and page tables, you will understand that long OS code is not arbitrary—it is structured to keep the processor running in a safe and ordered way.

---

## Why This Project Fits You

This project is especially suitable if you:

- want to understand how an OS actually boots
- want to learn assembly beyond trivial exercises
- want to understand low-level hardware interactions
- want to study system programming, drivers, memory, and debugging
- want to move from OS theory to practical implementation

If your goal is only to build a web app or a generic script, this may be a different direction. But if you want to understand how computers really work beneath the software layer, this project is a strong path.

---

## Why It Matters

After completing this project, you will no longer see software as abstract code floating in isolation. You will understand that everything eventually depends on:
- hardware behavior
- CPU execution model
- memory organization
- device interaction
- interrupt-driven coordination

That is the core of systems programming.

---

## Time Commitment

This is not a one-week project. A realistic pace is:

- 1-2 weeks: bootloader and startup flow
- 1-2 weeks: protected mode and GDT
- 1-2 weeks: interrupts and IDT
- 2-3 weeks: memory management and paging
- 2-3 weeks: process management and scheduling

A sustained period of 2-3 months is reasonable for a serious learner.

---

## Closing Thoughts

This project is not just a tutorial. It is a system-level exploration.

It starts at 0x7c00,
begins with BIOS,
continues through assembly and registers,
then gradually expands into protected mode, memory management, and interrupts.

The true enjoyment is not in writing a giant system in one go. It is in seeing your own code turn from a simple bootloader into something that prints output, handles events, and manages memory.

That journey is the real value of this project.

---

## Contact

- GitHub: https://github.com/Moyeswood/os-from-scratch
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

---

**Ready to explore the low-level world of computing? Let's begin. 🚀**
