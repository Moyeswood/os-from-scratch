# Learning Plan

This plan breaks the overall roadmap into a set of practical, actionable phases. The purpose is to keep the project measurable and focused on real understanding.

---

## Mission

Build a working understanding of how an operating system is constructed, starting with the startup path and then moving progressively into protected mode, interrupts, memory management, and process scheduling.

---

## Working method

- Work one phase at a time
- Validate every stage with code and debugging
- Record new understanding as it emerges
- Avoid expanding into breadth before the current layer is stable

---

## Phase 1: Foundations

### Tasks
- Learn CPU architecture basics
- Learn the difference between real mode and protected mode
- Learn x86 assembly fundamentals
- Understand BIOS startup flow
- Review memory layout and calling conventions

### Goal
Build a mental model of how execution begins and how memory is organized.

### Completion criteria
- Can explain BIOS startup flow
- Can explain the difference between real mode and protected mode
- Can read and write basic assembly

---

## Phase 2: Bootloader

### Tasks
- Analyze MBR structure
- Write a minimal boot sector
- Read disk sectors via BIOS
- Print a startup message
- Test execution in QEMU

### Goal
Construct a minimal bootloader and understand the startup flow.

### Completion criteria
- Bootloader runs in QEMU
- Prints a startup message
- Loads additional code beyond the first sector

---

## Phase 3: Protected Mode

### Tasks
- Learn GDT
- Understand segmentation
- Implement GDT setup
- Switch to protected mode
- Establish a kernel entry point

### Goal
Understand why protected mode is necessary and how it enables modern system execution.

### Completion criteria
- Can explain the purpose of the GDT
- Can switch into protected mode
- Can continue execution in 32-bit mode

---

## Phase 4: Interrupts and Exceptions

### Tasks
- Learn IDT
- Understand exception behavior
- Implement basic exception handlers
- Configure PIC
- Handle keyboard interrupts

### Goal
Make the system respond to both internal faults and external events.

### Completion criteria
- Understand interrupt flow
- Handle at least one exception
- Capture keyboard input

---

## Phase 5: Memory Management

### Tasks
- Learn paging basics
- Understand page table structure
- Configure page tables
- Enable paging
- Handle page faults

### Goal
Build the abstraction required for stable memory management.

### Completion criteria
- Understand virtual-to-physical mapping
- Configure page tables correctly
- Enable paging and handle faults

---

## Phase 6: Process and Scheduler

### Tasks
- Define a process structure
- Implement context switching
- Design a round-robin scheduler
- Add timer-driven scheduling
- Create multiple test processes

### Goal
Understand how multiple tasks share CPU time.

### Completion criteria
- Define a process model
- Implement context switching
- Run multiple tasks in sequence

---

## Phase 7: Expansion

### Tasks
- Implement a simple I/O driver
- Learn filesystem basics
- Build a minimal shell
- Learn the system call model

### Goal
Extend the kernel into a more complete computing environment.

### Completion criteria
- Handle simple I/O
- Execute basic commands
- Explain the user/kernel boundary

---

## Current priority

1. BIOS startup flow
2. x86 real mode
3. minimal bootloader
4. protected-mode transition
5. interrupt basics

---

## Next milestone

The next milestone is:

- minimal bootloader prints a message
- runs in QEMU
- prepares for the transition to protected mode

---

## 中文版本

# Learning Plan

这份计划把整个路线图拆成更小的、可执行的阶段。目标是让项目保持可度量，并保持对真实理解的关注。

---

## 项目使命

从启动代码出发，逐步建立对操作系统工作原理的理解，并依次推进到保护模式、中断、内存管理和进程调度。

---

## 工作方式

- 一次只做一个阶段
- 每一步都用代码和调试来验证
- 记录新学习到的内容
- 在当前层面稳定前，不扩展到更复杂的部分

---

## Phase 1: Foundations / 基础阶段

### 任务
- 学习 CPU 架构基础
- 理解实模式与保护模式的差异
- 学习 x86 汇编基础
- 理解 BIOS 启动流程
- 复习内存布局和调用约定

### 目标
建立对机器启动方式和内存组织方式的实践性理解。

### 完成标准
- 能解释 BIOS 启动流程
- 能解释实模式和保护模式差异
- 能读写基础汇编

---

## Phase 2: Bootloader / 引导程序阶段

### 任务
- 分析 MBR 结构
- 编写最小 boot sector
- 通过 BIOS 读取磁盘扇区
- 输出启动信息
- 在 QEMU 中测试执行

### 目标
构建最小 bootloader，并清楚理解启动流程。

### 完成标准
- 能在 QEMU 中运行
- 能输出启动信息
- 能加载第一个扇区之外的代码

---

## Phase 3: Protected Mode / 保护模式阶段

### 任务
- 学习 GDT
- 理解分段机制
- 实现 GDT 配置
- 切换到保护模式
- 建立内核入口

### 目标
理解保护模式为何必要，以及它如何支撑现代系统执行。

### 完成标准
- 能解释 GDT 的作用
- 能切换到保护模式
- 能在 32 位模式下继续执行

---

## Phase 4: Interrupts and Exceptions / 中断与异常阶段

### 任务
- 学习 IDT
- 理解异常机制
- 实现基础异常处理器
- 配置 PIC
- 处理键盘中断

### 目标
让系统能够响应内部错误和外部事件。

### 完成标准
- 理解中断流程
- 处理至少一个异常
- 能捕获键盘输入

---

## Phase 5: Memory Management / 内存管理阶段

### 任务
- 学习分页基础
- 理解页表结构
- 配置页表
- 启用分页
- 处理页错误

### 目标
建立稳定的内存模型。

### 完成标准
- 理解虚拟到物理映射
- 正确配置页表
- 启用分页并处理页错误

---

## Phase 6: Process and Scheduler / 进程与调度阶段

### 任务
- 定义进程结构
- 实现上下文切换
- 设计轮转调度器
- 添加定时器驱动调度
- 创建多个测试进程

### 目标
理解多个任务如何共享 CPU 时间。

### 完成标准
- 能定义进程模型
- 能实现上下文切换
- 能让多个任务轮流执行

---

## Phase 7: Expansion / 扩展阶段

### 任务
- 实现一个简单设备驱动
- 学习文件系统基础
- 构建一个最小 shell
- 学习系统调用机制

### 目标
把内核基础扩展到更高层次的系统服务。

### 完成标准
- 能处理简单 I/O
- 能执行基本命令
- 能解释用户态与内核态边界

---

## 当前优先级

1. BIOS 启动流程
2. x86 实模式
3. 最小 bootloader
4. GDT 和保护模式
5. 中断基础

---

## 下一里程碑

下一里程碑是：

- 最小 bootloader 能输出启动信息
- 能在 QEMU 中运行
- 为切换到保护模式做好准备
