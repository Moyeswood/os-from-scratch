# Learning Plan

This file is the practical task list for the OS From Scratch project. It breaks the full roadmap into small, actionable steps and keeps the work focused on learning outcomes.

## Project Mission

Learn how an operating system works by building the foundations of a minimal system from the bootloader upward.

## Working Approach

- One phase at a time
- Validate each stage with code and debugging
- Keep notes in the learning journal
- Expand only after the previous stage is stable

---

## Phase 1: Foundations

### Tasks

- Review CPU architecture basics
- Study real mode and protected mode differences
- Learn x86 register and memory layout fundamentals
- Understand BIOS startup flow
- Review assembly calling conventions and stack behavior

### Goal

Build a mental model of how a machine starts executing code and how memory is organized.

### Definition of Done

- You can explain the boot sequence and CPU mode transition
- You can describe memory layout and stack behavior
- You can write or read a basic assembly snippet confidently

---

## Phase 2: Bootloader

### Tasks

- Write a minimal boot sector
- Understand MBR layout and loading process
- Read disk sectors in assembly or C
- Print a message from the bootloader
- Transfer control to a second-stage kernel or payload

### Goal

Create a minimal, working bootloader that clearly demonstrates startup flow.

### Definition of Done

- The bootloader runs from disk successfully
- It prints output or indicates startup status
- It explains the handoff into a loaded program or kernel

---

## Phase 3: Protected Mode

### Tasks

- Study GDT and descriptor tables
- Learn how the CPU enters protected mode
- Set up basic segment descriptors
- Configure kernel and user segments
- Switch the system to 32-bit execution

### Goal

Understand the operating model that supports modern OS execution.

### Definition of Done

- You can describe the role of the GDT
- You can explain why protected mode matters
- You can boot into protected mode without confusion

---

## Phase 4: Interrupts and Exceptions

### Tasks

- Learn the interrupt vector and IDT
- Write a basic interrupt handler
- Study exception flow and fault handling
- Connect a keyboard or display interrupt to test behavior
- Understand how hardware triggers software routines

### Goal

Make the system responsive to hardware events and CPU faults.

### Definition of Done

- You understand the role of an interrupt table
- You can set up an IDT and explain handler entry
- You can describe how the kernel responds to interrupts

---

## Phase 5: Memory Management

### Tasks

- Study paging basics
- Understand page tables and page directories
- Learn virtual memory concepts
- Set up identity mapping or kernel mapping
- Explore how memory is divided between kernel and user regions

### Goal

Create the abstraction needed for stable memory management and process isolation.

### Definition of Done

- You can explain why paging exists
- You can describe page table translation
- You have a working basic mapping setup

---

## Phase 6: Process and Scheduler

### Tasks

- Define process metadata and states
- Learn basic context switching
- Design a round-robin scheduler skeleton
- Explore user mode vs kernel mode boundaries
- Understand how execution flow changes between tasks

### Goal

Understand the heart of multitasking inside an operating system.

### Definition of Done

- You can explain process state transitions
- You can describe the purpose of context switching
- You can sketch a simple scheduler model

---

## Phase 7: Expansion

### Tasks

- Experiment with simple I/O drivers
- Learn basic filesystem concepts
- Create a simple terminal or shell
- Explore command dispatch
- Connect user-visible services with kernel features

### Goal

Turn the minimal kernel into a more complete and understandable system.

### Definition of Done

- You can connect hardware and kernel features into a usable service layer
- You can explain how system calls and user programs fit into the model

---

## Current Priority

The current priority is to complete these items first:

1. real mode and startup fundamentals
2. bootloader design and testing
3. GDT and protected mode basics
4. interrupt and exception handling foundations

---

## Next Milestone

The next milestone is to build a minimal bootloader that:

- runs in real mode
- loads additional code or payload
- prints a startup message
- transitions toward protected mode preparation

---

## Notes

This plan should evolve as the project grows. The main purpose is to keep the work practical, measurable, and aligned with the roadmap.

