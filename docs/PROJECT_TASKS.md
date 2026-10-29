# Project Tasks / 项目任务清单

This document is the final in-repo task board template for the OS From Scratch project.

This file matches the structure used by GitHub Projects and helps keep learning milestones visible and actionable.

---

## Project Board Structure / 看板结构

Recommended columns / 建议列：
- Backlog / 待办
- Research / 调研
- In Progress / 进行中
- Validation / 验证中
- Done / 已完成

Recommended milestones / 建议阶段：
- Phase 1: Foundations / 基础阶段
- Phase 2: Bootloader / 引导程序阶段
- Phase 3: Protected Mode / 保护模式阶段
- Phase 4: Interrupts / 中断阶段
- Phase 5: Memory Management / 内存管理阶段
- Phase 6: Process & Scheduler / 进程与调度阶段
- Phase 7: Expansion / 扩展阶段

---

## Phase 1: Foundations / 基础阶段

### Task 1: Learn BIOS Startup Flow / 学习 BIOS 启动流程
- Status: Backlog
- Priority: High
- Goal: Understand the boot sequence from power-on to bootloader execution
- Key concepts: POST, BIOS initialization, bootstrap flow
- Deliverable: startup notes
- Definition of Done:
  - [ ] Can explain BIOS startup flow
  - [ ] Can describe the bootloader handoff
  - [ ] Notes are documented

### Task 2: Learn x86 Real Mode / 学习 x86 实模式
- Status: Backlog
- Priority: High
- Goal: Understand register usage and memory addressing in real mode
- Key concepts: segment registers, real mode addressing, memory layout
- Deliverable: real mode summary
- Definition of Done:
  - [ ] Can explain CPU registers
  - [ ] Can describe memory layout
  - [ ] Can explain segment:offset logic

### Task 3: Study Assembly Basics / 学习汇编基础
- Status: Backlog
- Priority: High
- Goal: Learn x86 assembly and stack behavior
- Key concepts: mov, jmp, call, ret, stack, flags
- Deliverable: assembly sample code
- Definition of Done:
  - [ ] Can write basic assembly code
  - [ ] Can explain stack behavior
  - [ ] Code is commented

### Task 4: Learn CPU and Memory Fundamentals / 学习 CPU 与内存基础
- Status: Backlog
- Priority: Medium
- Goal: Understand how instruction execution and memory layout work together
- Key concepts: address space, stack, execution flow
- Deliverable: CPU and memory notes
- Definition of Done:
  - [ ] Can explain memory layout
  - [ ] Can explain execution flow
  - [ ] Diagram is created

---

## Phase 2: Bootloader / 引导程序阶段

### Task 5: Analyze MBR Structure / 分析 MBR 结构
- Status: Backlog
- Priority: High
- Goal: Understand the first disk sector and boot process
- Key concepts: MBR, partition table, boot signature, BIOS load
- Deliverable: MBR notes
- Definition of Done:
  - [ ] Can explain first-sector structure
  - [ ] Can explain BIOS loading behavior
  - [ ] Notes are complete

### Task 6: Write Minimal Boot Sector / 编写最小启动扇区
- Status: Backlog
- Priority: High
- Goal: Create a minimal boot sector that prints output
- Key concepts: 0x7C00, BIOS interrupt, startup message
- Deliverable: bootloader.asm
- Definition of Done:
  - [ ] Runs in QEMU
  - [ ] Prints startup output
  - [ ] Code is documented

### Task 7: Implement Disk Reading / 实现磁盘读取
- Status: Backlog
- Priority: High
- Goal: Read additional sectors from disk using BIOS
- Key concepts: int 0x13, CHS/LBA, DAP, second-stage loading
- Deliverable: disk read routine
- Definition of Done:
  - [ ] Can load more code from disk
  - [ ] Can transfer control to new code
  - [ ] Test log exists

### Task 8: Print from Boot Code / 从引导代码输出显示
- Status: Backlog
- Priority: Medium
- Goal: Display output from boot code
- Key concepts: BIOS video service, cursor, string output
- Deliverable: display functions
- Definition of Done:
  - [ ] Can print text to screen
  - [ ] Display is readable
  - [ ] Output is stable

### Task 9: Test Bootloader in QEMU / 在 QEMU 中测试 bootloader
- Status: Backlog
- Priority: High
- Goal: Validate startup behavior in the emulator
- Key concepts: QEMU, disk image, boot debugging
- Deliverable: validation log
- Definition of Done:
  - [ ] Bootloader runs in QEMU
  - [ ] Output is captured
  - [ ] Boot log exists

---

## Phase 3: Protected Mode / 保护模式阶段

### Task 10: Study GDT / 学习 GDT
- Status: Backlog
- Priority: High
- Goal: Understand the Global Descriptor Table
- Key concepts: GDT, segment descriptors, privilege levels
- Deliverable: GDT summary
- Definition of Done:
  - [ ] Can explain GDT purpose
  - [ ] Can describe descriptor layout
  - [ ] Notes are complete

### Task 11: Implement GDT Setup / 实现 GDT 配置
- Status: Backlog
- Priority: High
- Goal: Create and load a valid GDT
- Key concepts: lgdt, segment definitions, descriptors
- Deliverable: GDT setup code
- Definition of Done:
  - [ ] GDT loads successfully
  - [ ] Segments are configured
  - [ ] Code is documented

### Task 12: Switch to Protected Mode / 切换到保护模式
- Status: Backlog
- Priority: High
- Goal: Enter 32-bit protected mode
- Key concepts: CR0, far jump, mode switch
- Deliverable: mode switch code
- Definition of Done:
  - [ ] CPU enters protected mode
  - [ ] Execution continues in 32-bit mode
  - [ ] Debug output confirms success

### Task 13: Set Up Kernel Entry Point / 建立内核入口
- Status: Backlog
- Priority: High
- Goal: Define the kernel entry after mode switch
- Key concepts: kernel stack, control flow, entry point
- Deliverable: kernel entry routine
- Definition of Done:
  - [ ] Execution reaches kernel code
  - [ ] Entry point is stable
  - [ ] Kernel output is visible

---

## Phase 4: Interrupts and Exceptions / 中断与异常阶段

### Task 14: Study IDT / 学习 IDT
- Status: Backlog
- Priority: High
- Goal: Understand interrupt descriptors and handlers
- Key concepts: IDT, exceptions, interrupt vectors
- Deliverable: IDT notes
- Definition of Done:
  - [ ] Can explain IDT structure
  - [ ] Can describe exception flow
  - [ ] Diagram is complete

### Task 15: Implement IDT Setup / 实现 IDT 配置
- Status: Backlog
- Priority: High
- Goal: Create and load the IDT
- Key concepts: lidt, gate descriptors, interrupt setup
- Deliverable: IDT setup code
- Definition of Done:
  - [ ] IDT loads successfully
  - [ ] Interrupts are configured
  - [ ] Code is stable

### Task 16: Implement Basic Exception Handlers / 实现基础异常处理器
- Status: Backlog
- Priority: High
- Goal: Handle standard exceptions safely
- Key concepts: division by zero, page fault, exception handling
- Deliverable: exception handler code
- Definition of Done:
  - [ ] Common exceptions are handled
  - [ ] Error information is printed
  - [ ] Output makes sense

### Task 17: Handle Keyboard Interrupt / 处理键盘中断
- Status: Backlog
- Priority: Medium
- Goal: Read keyboard input through IRQ
- Key concepts: IRQ1, keyboard controller, scan codes
- Deliverable: keyboard handler
- Definition of Done:
  - [ ] Keyboard interrupts are detected
  - [ ] Input is captured
  - [ ] Display output works

---

## Phase 5: Memory Management / 内存管理阶段

### Task 18: Study Paging Basics / 学习分页基础
- Status: Backlog
- Priority: High
- Goal: Understand virtual memory and paging
- Key concepts: page, frame, address translation, page table
- Deliverable: paging notes
- Definition of Done:
  - [ ] Can explain page mapping
  - [ ] Can describe virtual memory concept
  - [ ] Diagram is clear

### Task 19: Implement Page Tables / 实现页表
- Status: Backlog
- Priority: High
- Goal: Build page directories and tables
- Key concepts: CR3, page directory, page tables, identity mapping
- Deliverable: page table code
- Definition of Done:
  - [ ] Page tables are correctly configured
  - [ ] Entries are formatted as expected
  - [ ] Memory map is written down

### Task 20: Enable Paging / 启用分页
- Status: Backlog
- Priority: High
- Goal: Turn on CPU paging
- Key concepts: CR0.PG, page enable, page fault behavior
- Deliverable: paging enable routine
- Definition of Done:
  - [ ] Paging is enabled
  - [ ] Execution continues correctly
  - [ ] No immediate crash

### Task 21: Implement Page Fault Handler / 实现页错误处理
- Status: Backlog
- Priority: High
- Goal: Handle missing or invalid pages
- Key concepts: page fault, CR2, fault diagnosis
- Deliverable: page fault handler
- Definition of Done:
  - [ ] Faults are identified
  - [ ] Faulting address is available
  - [ ] Error is reported clearly

---

## Phase 6: Process and Scheduler / 进程与调度阶段

### Task 22: Define Process Structure / 定义进程结构
- Status: Backlog
- Priority: High
- Goal: Define process and task metadata
- Key concepts: process state, CPU context, ready/running/blocked
- Deliverable: process structure
- Definition of Done:
  - [ ] Process model is defined
  - [ ] States are clear
  - [ ] Documentation exists

### Task 23: Implement Context Switching / 实现上下文切换
- Status: Backlog
- Priority: High
- Goal: Save and restore CPU state between processes
- Key concepts: register save, stack switching, context restore
- Deliverable: context switch routine
- Definition of Done:
  - [ ] CPU state is saved correctly
  - [ ] One task switches to another
  - [ ] No register corruption

### Task 24: Design Round-Robin Scheduler / 设计轮转调度器
- Status: Backlog
- Priority: High
- Goal: Schedule multiple tasks fairly
- Key concepts: ready queue, time slice, scheduler loop
- Deliverable: scheduler skeleton
- Definition of Done:
  - [ ] Multiple tasks are queued
  - [ ] Scheduler fairness is present
  - [ ] Tasks run in sequence

### Task 25: Add Timer Interrupt for Scheduling / 添加定时器中断调度
- Status: Backlog
- Priority: High
- Goal: Trigger scheduling with timer interrupts
- Key concepts: PIT, IRQ0, preemptive scheduling
- Deliverable: timer-driven scheduler
- Definition of Done:
  - [ ] Timer interrupt invokes scheduler
  - [ ] Tasks switch automatically
  - [ ] Logs show scheduling behavior

---

## Phase 7: Expansion / 扩展阶段

### Task 26: Implement Simple I/O Driver / 实现简单 I/O 驱动
- Status: Backlog
- Priority: Medium
- Goal: Add a basic device driver layer
- Key concepts: device initialization, port I/O, interrupt-driven I/O
- Deliverable: keyboard or serial driver
- Definition of Done:
  - [ ] Driver initializes device
  - [ ] Reads/writes correctly
  - [ ] Output works reliably

### Task 27: Learn Filesystem Basics / 学习文件系统基础
- Status: Backlog
- Priority: Medium
- Goal: Understand storage structure and file organization
- Key concepts: inode, directory, metadata, blocks
- Deliverable: filesystem notes
- Definition of Done:
  - [ ] Can explain file organization
  - [ ] Understand basic metadata layout
  - [ ] Notes are complete

### Task 28: Build Minimal Shell / 构建最小 shell
- Status: Backlog
- Priority: Medium
- Goal: Accept commands and dispatch them
- Key concepts: command parsing, user input, process launching
- Deliverable: shell implementation
- Definition of Done:
  - [ ] Shell accepts input
  - [ ] Commands parse successfully
  - [ ] A simple command runs

### Task 29: Introduce System Call Flow / 引入系统调用机制
- Status: Backlog
- Priority: Low
- Goal: Understand user-to-kernel transitions
- Key concepts: system call numbers, privilege boundary
- Deliverable: syscall interface
- Definition of Done:
  - [ ] User process can invoke kernel function
  - [ ] Parameters pass correctly
  - [ ] System call is tested

---

## Final recommendation / 最终建议

Start with the first three phases:
- Foundations
- Bootloader
- Protected Mode

This is the smallest meaningful path from startup to kernel entry.

The project should be advanced incrementally, with each phase validated through real code, logs, and notes.

---

## Final checklist / 最终检查项

- [ ] README exists
- [ ] ROADMAP exists
- [ ] LEARNING_PLAN exists
- [ ] docs/LEARNING_JOURNEY.md exists
- [ ] GitHub Projects board is created
- [ ] Milestones are configured
- [ ] First tasks are added to Backlog
- [ ] First milestone is in progress
