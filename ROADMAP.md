# Roadmap

This roadmap defines the long-term learning path for the OS From Scratch project.

The project is intentionally structured in stages so that each layer builds naturally on the previous one. The goal is not speed, but clarity and understanding.

---

## Project objective

Understand how an operating system is built from the hardware layer upward, beginning with startup and bootstrapping, then progressing through protected mode, interrupts, memory management, scheduling, and expansion.

---

## Main phases

1. Foundations
2. Bootloader
3. Protected Mode
4. Interrupts and Exceptions
5. Memory Management
6. Process and Scheduler
7. Expansion

---

## Phase 1: Foundations

### Goal
Understand the machine model that the OS depends on.

### Topics
- CPU architecture basics
- real mode behavior
- memory layout
- BIOS startup flow
- assembly fundamentals

### Deliverables
- notes on CPU and memory basics
- real mode examples
- boot process notes

### Completion criteria
- Can explain CPU execution flow
- Can describe memory and stack behavior
- Can read and write basic assembly

---

## Phase 2: Bootloader

### Goal
Build the first executable software layer that loads and hands control to the next stage.

### Topics
- MBR and boot sector
- BIOS interrupt services
- disk reading
- second-stage loading
- QEMU-based validation

### Deliverables
- minimal boot sector
- bootloader that prints output
- code that loads additional sectors

### Completion criteria
- Can explain boot sector structure
- Can load additional code from disk
- Can run the bootloader in QEMU

---

## Phase 3: Protected Mode

### Goal
Understand the transition from real mode to the modern protected execution model.

### Topics
- GDT
- segment descriptors
- privilege levels
- switching to 32-bit mode
- kernel entry

### Deliverables
- GDT configuration
- protected mode switch
- kernel entry point

### Completion criteria
- Can explain the role of the GDT
- Can enter protected mode
- Can continue execution in a protected environment

---

## Phase 4: Interrupts and Exceptions

### Goal
Understand how the CPU and hardware communicate with software.

### Topics
- IDT
- exception handling
- interrupt flow
- keyboard input
- PIC setup

### Deliverables
- IDT configuration
- exception handler
- keyboard interrupt example

### Completion criteria
- Can explain interrupt flow
- Can handle at least one exception
- Can read keyboard input through a hardware interrupt

---

## Phase 5: Memory Management

### Goal
Build the abstraction needed for memory isolation and resource management.

### Topics
- paging
- page tables
- virtual memory
- page faults

### Deliverables
- paging setup
- page table code
- page fault handler

### Completion criteria
- Can explain virtual-to-physical mapping
- Can configure page tables
- Can enable paging safely

---

## Phase 6: Process and Scheduler

### Goal
Understand how multiple tasks share CPU time and state.

### Topics
- process structure
- context switching
- round-robin scheduler
- timer-driven scheduling

### Deliverables
- process structure
- context switch routine
- scheduler prototype

### Completion criteria
- Can describe task state transitions
- Can write a basic context switch
- Can schedule multiple tasks in sequence

---

## Phase 7: Expansion

### Goal
Move from low-level kernel fundamentals toward a more complete system.

### Topics
- device drivers
- filesystem basics
- shell
- system call design

### Deliverables
- simple driver
- minimal shell
- system call notes

### Completion criteria
- Can explain the dependency between low-level mechanisms and higher-level services
- Can build a small user-facing interface
- Can describe the user/kernel boundary

---

## Roadmap philosophy

- Start small
- Validate each layer with code
- Build one layer at a time
- Keep the system understandable
- Move from hardware to abstraction

---

## 中文版本

# Roadmap

这份路线图定义了 OS From Scratch 项目的长期学习路径。

这个项目被设计成分阶段推进的方式，所以每一层知识都能自然建立在前一层之上。重点不是急着做出一个完整系统，而是让学习建立在清晰理解之上。

---

## 项目目标

从硬件层开始，逐步理解操作系统是如何从启动流程发展到保护模式、中断、内存管理、调度以及更高层系统能力的。

---

## 学习阶段

1. Foundations
2. Bootloader
3. Protected Mode
4. Interrupts and Exceptions
5. Memory Management
6. Process and Scheduler
7. Expansion

---

## Phase 1: Foundations / 基础阶段

### 目标
理解操作系统运行依赖的机器模型。

### 主题
- CPU 架构基础
- 实模式行为
- 内存布局
- BIOS 启动流程
- 汇编基础

### 产出
- CPU 和内存基础笔记
- 实模式示例
- 启动流程总结

### 完成标准
- 能解释 CPU 执行流程
- 能描述内存和栈行为
- 能读写基础汇编

---

## Phase 2: Bootloader / 引导程序阶段

### 目标
构建第一层可执行代码，用于加载并交出控制权。

### 主题
- MBR
- boot sector layout
- BIOS 中断服务
- 磁盘读取
- second-stage loading

### 产出
- 最小 boot sector
- 能输出启动信息的 bootloader
- 能加载额外扇区的代码

### 完成标准
- 能解释 boot sector 结构
- 能从磁盘加载额外代码
- 能在 QEMU 中运行 bootloader

---

## Phase 3: Protected Mode / 保护模式阶段

### 目标
理解为什么现代系统必须切换到保护模式。

### 主题
- GDT
- 段描述符
- 权限级别
- 32 位模式切换
- 内核入口

### 产出
- GDT 配置
- 保护模式切换代码
- 内核入口

### 完成标准
- 能解释 GDT 的作用
- 能切换到保护模式
- 能在受保护环境中继续执行

---

## Phase 4: Interrupts and Exceptions / 中断与异常阶段

### 目标
理解 CPU 和硬件如何与软件通信，并如何处理错误。

### 主题
- IDT
- 异常处理
- 中断流程
- 键盘中断
- PIC 配置

### 产出
- IDT 配置
- 异常处理器
- 键盘中断示例

### 完成标准
- 能解释中断流程
- 能处理至少一个异常
- 能通过硬件中断读取键盘输入

---

## Phase 5: Memory Management / 内存管理阶段

### 目标
建立内存隔离和资源管理所需的抽象。

### 主题
- 分页
- 页表
- 虚拟内存
- page fault

### 产出
- 分页代码
- 页表配置
- 页错误处理器

### 完成标准
- 能解释虚拟地址到物理地址的映射
- 能配置页表
- 能安全启用分页

---

## Phase 6: Process and Scheduler / 进程与调度阶段

### 目标
理解多个任务如何共享 CPU 时间与状态。

### 主题
- 进程结构
- 上下文切换
- 轮转调度器
- 定时器调度

### 产出
- 进程结构
- 上下文切换例程
- 调度器原型

### 完成标准
- 能描述任务状态转换
- 能实现基础上下文切换
- 能让多个任务轮流执行

---

## Phase 7: Expansion / 扩展阶段

### 目标
从底层内核基础过渡到更完整的系统思维。

### 主题
- 设备驱动
- 文件系统基础
- shell
- 系统调用设计

### 产出
- 简单驱动
- 最小 shell
- 系统调用概念笔记

### 完成标准
- 能解释低层机制和高层服务之间的关系
- 能构建一个简单用户接口
- 能描述用户态和内核态边界

---

## 路线哲学

- 从小处开始
- 每一层都用代码验证
- 一层一层构建
- 保持系统可理解
- 从硬件到抽象逐步推进
