# OS From Scratch Roadmap

This roadmap is the long-term guide for the project. It defines the learning direction, major milestones, and the expected outcomes of each stage.

## Project Goal

Build a working understanding of how an operating system is created from the ground up, starting from bootloading and progressing toward protected mode, memory management, interrupts, process scheduling, and basic system design.

## Core Principles

- Learn by building, not just reading.
- Start from the simplest possible startup path.
- Validate every stage with code and debugging.
- Understand the hardware before the abstraction.
- Build the system layer by layer.

## Roadmap Overview

1. Foundations: CPU, memory, startup flow, and machine architecture
2. Bootloader: real mode, MBR, loading the kernel
3. Protected Mode: GDT, segmentation, privilege levels
4. Interrupts and Exceptions: IDT, keyboard, screen, fault handling
5. Memory Management: paging, page tables, virtual memory
6. Process Management: task design, scheduler, context switching
7. Expansion: drivers, filesystems, shell, userland basics

---

## Phase 1: Foundations

### Goal

Understand the hardware and runtime model that an operating system depends on.

### Topics

- CPU architecture and registers
- Real mode vs protected mode
- RAM layout and memory map
- BIOS startup flow
- Boot sector structure
- Assembly basics and function calling conventions

### Deliverables

- A working note on x86 architecture basics
- A minimal real-mode boot sector experiment
- A clear model of how a machine enters execution

### Recommended Reading

- Computer organization and architecture basics
- x86 assembly references
- The beginning chapters of operating system textbooks

---

## Phase 2: Bootloader

### Goal

Build a minimal bootloader that can load and transfer control to a kernel or test payload.

### Topics

- BIOS interrupt services
- Disk reading via boot sector logic
- Bootloader entry point
- Memory layout during startup
- Transition from boot code to kernel code

### Deliverables

- Minimal bootloader that prints a message
- Bootloader that loads secondary stages or kernel payloads
- Working understanding of startup memory and control flow

### Definition of Done

- You can explain how BIOS hands control to code
- You can read a sector from disk in assembly or C
- You can explain why the bootloader exists

---

## Phase 3: Protected Mode

### Goal

Move from real mode into a protected execution model.

### Topics

- GDT and descriptors
- Segmentation
- Privilege levels
- Kernel and user segments
- Switching from 16-bit mode to 32-bit mode

### Deliverables

- Working GDT setup
- A kernel entry in protected mode
- A clear explanation of runtime privilege boundaries

### Definition of Done

- You can describe the purpose of the GDT
- You can explain why real mode is insufficient for modern OS design
- You can switch execution into protected mode cleanly

---

## Phase 4: Interrupts and Exceptions

### Goal

Make the system responsive and capable of handling device and CPU-generated events.

### Topics

- IDT and interrupt descriptors
- Hardware and software interrupts
- Exceptions and fault handling
- Keyboard and screen I/O
- PIC / APIC overview

### Deliverables

- IDT configuration
- Basic interrupt handler skeleton
- Keyboard or display output flow from interrupt events

### Definition of Done

- You understand the purpose of interrupts
- You can explain the IDT and exception flow
- You can hook a basic interrupt handler and test it

---

## Phase 5: Memory Management

### Goal

Create a stable memory model for the kernel and user processes.

### Topics

- Paging model
- Page tables and page directories
- Virtual memory concepts
- Memory mapping and identity mapping
- Kernel heap and user memory boundaries

### Deliverables

- Page tables setup
- Simple memory mapping for kernel regions
- Basic explanation of virtual memory design

### Definition of Done

- You understand why paging exists
- You can describe page table structures and translation steps
- You can enable paging in a simple kernel setup

---

## Phase 6: Process Management

### Goal

Define the abstraction for tasks and execution flow inside the OS.

### Topics

- Process structure and metadata
- Context switching
- Scheduling policy basics
- Task states
- User mode and kernel mode boundaries

### Deliverables

- A task structure definition
- Scheduler stub or round-robin design
- Simple process switch demonstration

### Definition of Done

- You can explain the difference between process and thread at a basic level
- You can describe context switching in simple terms
- You can sketch a scheduler design that matches the kernel

---

## Phase 7: Expansion and System Growth

### Goal

Move beyond the kernel core and add the basics needed for a usable operating system.

### Topics

- Drivers
- Filesystem basics
- Terminal and shell
- Userland tools
- Process and I/O interaction

### Deliverables

- Minimal shell or command interface
- Basic memory/file abstraction experiments
- Exploration of OS service layers

### Definition of Done

- You understand how higher-level system services depend on lower-level kernel features
- You can connect interrupt, memory, and scheduler concepts into a simple user-facing system

---

## Suggested Project Timeline

### Early Stage

- Real mode and bootloader fundamentals
- Assembly and startup understanding

### Mid Stage

- Protected mode
- IDT and interrupts
- Paging

### Later Stage

- Scheduler
- Process model
- Userland interaction

---

## Recommended Books and References

- Deep understanding of computer architecture and low-level systems
- x86 assembly references
- Operating systems textbooks
- Linker and loader concepts
- C programming and foundational system documentation

---

## Final Objective

The goal is not to build a perfect commercial OS in one step. The goal is to understand the architecture and design decisions behind an operating system by implementing the simplest meaningful system pieces and learning from each stage.

---

## Project Philosophy

- Start small
- Verify the fundamentals
- Build with understanding
- Keep the system debuggable
- Grow incrementally

This roadmap is meant to be updated as the project evolves.

