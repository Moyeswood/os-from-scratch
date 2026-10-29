# Learning Journey / 学习日志

This journal records the project’s progress, understanding, obstacles, and conclusions as the work develops.

这份日志记录项目的推进过程、理解、困难与结论，以便后续回看和复盘。

---

## Purpose / 目标

- Record practical learning progress / 记录实际学习进度
- Track project milestones / 跟踪项目里程碑
- Capture confusion and breakthroughs / 记录困惑与突破
- Preserve technical observations / 保存技术观察与理解

---

## Current Focus / 当前重点

Current focus:
- BIOS startup flow
- real mode understanding
- minimal bootloader
- QEMU validation

当前重点：
- BIOS 启动流程
- 实模式理解
- 最小 bootloader
- QEMU 验证

---

## Phase 1: Foundations / 基础阶段

### Notes / 笔记
- Understand the role of BIOS and bootloader
- Learn x86 register usage and memory layout
- Practice simple assembly patterns

### Observations / 观察
- The startup path is simpler than it seems once traced from power-on
- Real mode is a crucial concept for understanding later transitions

---

## Phase 2: Bootloader / 引导程序阶段

### Notes / 笔记
- Boot sector is loaded at 0x7C00
- BIOS provides basic disk and video services
- Output can be achieved even before a full kernel exists

### Observations / 观察
- A minimal bootloader is the first real proof that low-level code works
- QEMU is invaluable for validating startup behavior

---

## Phase 3: Protected Mode / 保护模式阶段

### Notes / 笔记
- GDT is required for protected execution
- Switching from real mode to protected mode is a critical transition
- Kernel entry point must be defined carefully

### Observations / 观察
- Protected mode changes how memory and privilege are managed
- This is a major turning point in understanding OS design

---

## Phase 4: Interrupts and Exceptions / 中断与异常阶段

### Notes / 笔记
- IDT is essential for handling hardware and software events
- Exceptions reveal the CPU state and fault conditions
- Keyboard interrupt handling turns hardware events into software input

### Observations / 观察
- Interrupts are one of the most important OS interfaces
- They unify CPU events, hardware signals, and software responsiveness

---

## Phase 5: Memory Management / 内存管理阶段

### Notes / 笔记
- Paging introduces abstraction between logical and physical addresses
- Page tables are essential for memory isolation and management
- Page faults reveal absent or invalid mappings

### Observations / 观察
- Virtual memory is one of the central OS abstractions
- Memory management creates the foundation for process isolation

---

## Phase 6: Process and Scheduler / 进程与调度阶段

### Notes / 笔记
- Multiple tasks require CPU context preservation
- Scheduling turns a single CPU into a time-shared execution environment
- Timer interrupts are essential for preemptive multitasking

### Observations / 观察
- The scheduler is one of the core ideas that makes an OS feel like an OS
- Process switching is a direct manifestation of multitasking

---

## Phase 7: Expansion / 扩展阶段

### Notes / 笔记
- Drivers connect hardware devices to kernel logic
- Filesystems create persistent, structured storage
- Shells turn the system into an interactive interface

### Observations / 观察
- These layers depend on the lower kernel foundations
- The higher-level system emerges naturally from lower-level primitives

---

## Summary / 总结

This project is best understood as a layered build:
- bootloader first
- protected mode second
- interrupts and memory next
- scheduling after that
- user-facing features last

这个项目最适合按分层方式理解：
- 先 bootloader
- 再保护模式
- 然后中断和内存
- 随后调度
- 最后再扩展更高层能力

The goal is not speed but understanding.

目标不是快，而是理解。
