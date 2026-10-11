# OS From Scratch

<p align="center">
  <img src="https://img.shields.io/badge/Status-Learning%20in%20Progress-4CAF50?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Focus-Bootloader%20%26%20Kernel-2196F3?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Language-x86%20Assembly%20%2F%20C-FF9800?style=for-the-badge" />
</p>

A hands-on project to understand how an operating system is built from the ground up.

This repository documents a practical study of low-level system design, starting from the boot process and moving toward protected mode, interrupts, paging, process management, and higher-level OS concepts.

The goal is not to build a polished commercial OS in one go, but to develop a deep and usable understanding of how modern operating systems actually work.

---

## Why this project exists

I want to understand the core questions behind system design:

- Why does a computer boot?
- How does BIOS hand control to the system?
- What is the purpose of real mode and protected mode?
- Why are interrupts and exceptions so important?
- How does memory become virtual memory?
- How does a kernel manage tasks and resources?

This project exists to answer those questions through code, notes, and experiments.

---

## Learning roadmap

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
Drivers / I/O / Shell
```

---

## Current focus

The project is currently focused on:

- BIOS startup flow
- x86 real mode fundamentals
- minimal bootloader implementation
- QEMU-based validation
- preparation for protected mode and interrupts

---

## Repository structure

```text
README.md
ROADMAP.md
LEARNING_PLAN.md
docs/
  LEARNING_JOURNEY.md
```

---

## Documentation

- [ROADMAP.md](ROADMAP.md) — long-term learning path and milestones
- [LEARNING_PLAN.md](LEARNING_PLAN.md) — phased task list and execution goals
- [docs/LEARNING_JOURNEY.md](docs/LEARNING_JOURNEY.md) — notes, experiments, and progress log

---

## Design principles

- Start from the simplest possible execution model
- Validate every concept with code
- Understand the hardware before the abstraction
- Build one layer at a time
- Keep the project debuggable and explainable

---

## Project status

This project is currently in the foundational boot and startup phase, with the near-term goal of getting a minimal bootloader running and preparing for protected mode.

---

## 中文说明

# OS From Scratch

<p align="center">
  <img src="https://img.shields.io/badge/状态-学习中-4CAF50?style=for-the-badge" />
  <img src="https://img.shields.io/badge/重点-Bootloader%20%26%20Kernel-2196F3?style=for-the-badge" />
  <img src="https://img.shields.io/badge/语言-x86%20Assembly%20%2F%20C-FF9800?style=for-the-badge" />
</p>

一个从零理解操作系统的实践型学习项目。

这个仓库记录了一个从底层系统设计出发的学习过程，内容从计算机启动开始，逐步走向：

- BIOS 启动流程
- x86 实模式与保护模式
- bootloader 设计
- 中断与异常
- 分页与虚拟内存
- 进程调度与任务切换
- 更高层的系统抽象与驱动思维

这里的目标不是一开始就做一个完整商业内核，而是建立正确的底层理解，并逐步构建操作系统设计的核心观念。

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
Drivers / I/O / Shell
```

---

## 当前阶段

当前重点在于：

- BIOS 启动流程
- x86 实模式基础
- 最小 bootloader 开发
- QEMU 中的运行验证
- 为保护模式和中断处理做准备

---

## 文档入口

- [ROADMAP.md](ROADMAP.md) — 长期路线图
- [LEARNING_PLAN.md](LEARNING_PLAN.md) — 分阶段任务清单
- [docs/LEARNING_JOURNEY.md](docs/LEARNING_JOURNEY.md) — 学习日志与记录

---

## 设计原则

- 从最简单的执行路径开始
- 用代码验证每个概念
- 先理解硬件，再理解抽象
- 一层一层构建
- 保持项目可调试、可解释、可扩展

---

## 项目状态

目前处于基础引导与启动学习阶段，近期目标是让最小的 bootloader 能正常运行，并为后续进入保护模式和中断处理打下基础。
