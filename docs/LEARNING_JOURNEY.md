# OS From Scratch - Learning Journey & Milestones

[中文](#中文) | [English](#english)

---

# 中文

## 📖 项目学习日志

这是一份记录，记录了从零开始实现操作系统的每一步学习过程、遇到的问题、得到的insights，以及项目的进展里程碑。

这不是一份技术文档，而是一份**学习者的日志**——用来追踪你的理解深度、遇见的问题、解决的办法，以及最终的"顿悟时刻"。

---

## 🎯 为什么记录学习过程？

很多人做项目只关注"最终成果"，却忽略了"学习过程本身"的价值。而对于一个如此复杂的系统工程，记录过程很重要，因为：

1. **回顾时能清楚看到自己的成长**
2. **帮助后来者理解同样的困难**
3. **记录下每个阶段的"顿悟时刻"**
4. **对比之前和之后的理解有多大差异**

---

## 📅 项目里程碑时间线

### 🔷 第一阶段：引导加载器的世界（第 1-2 周）

#### ✅ 2026-10-11 - 项目初始化

**做了什么：**
- 在 GitHub 创建 os-from-scratch 仓库
- 建立项目基础结构
- 写好详尽的 README.md（中英文）
- 准备开发环境和工具链

**学到的：**
- GitHub 仓库的正确结构
- 项目文档的重要性
- 开源项目的风格规范

**遇到的问题：**
- 无（这是准备阶段）

**当时的心情：**
🎉 充满期待。觉得自己真的要开始干这事儿了。

---

#### 📝 第 1 周目标：理解计算机启动过程

**学习内容：**
- [ ] x86 实模式下的寻址方式
- [ ] BIOS 启动流程（POST → 引导加载）
- [ ] MBR（Master Boot Record）的结构
- [ ] 0x7c00 地址的意义
- [ ] int 0x13（磁盘读取中断）的使用

**计划实现：**
- [ ] 最小 MBR 引导代码
- [ ] 磁盘读取函数（disk_load）
- [ ] 字符串打印函数（print_string）
- [ ] 内核加载和跳转

**学习资料：**
- OSDev.org 的 Bootloader 教程
- Bran's Kernel Development 教程第 1 章
- x86 汇编语言基础

**预期的困难：**
- 汇编语法不熟悉
- 二进制布局和地址计算
- 调试这么低层的代码

**当前进度：** 🔄 进行中

---

### 🔷 第二阶段：保护模式的工作原理（第 3-4 周）

#### 📝 第 2-3 周目标：进入 32 位的世界

**学习内容（计划）：**
- [ ] GDT（全局描述符表）的结构
- [ ] 段描述符和权限级别
- [ ] CR0 寄存器的 PE 位
- [ ] 长跳转指令（jmp far）
- [ ] 32 位寄存器和寻址

**计划实现：**
- [ ] GDT 数据结构
- [ ] GDT 加载指令（lgdt）
- [ ] 保护模式切换代码
- [ ] 32 位的 hello world

**预期的困难：**
- GDT 的每一个字节含义
- 段选择子的计算
- 从实模式到保护模式的飞跃

**当前进度：** ⏳ 即将开始

---

### 🔷 第三阶段：中断驱动系统（第 5-7 周）

#### 📝 第 4-5 周目标：让系统响应硬件事件

**学习内容（计划）：**
- [ ] IDT（中断描述符表）
- [ ] 中断和异常的区别
- [ ] ISR（中断服务程序）
- [ ] 8259 中断控制器
- [ ] IRQ 与向量号的映射

**计划实现：**
- [ ] IDT 数据结构
- [ ] 基本的中断处理框架
- [ ] 时钟中断处理（IRQ0）
- [ ] 键盘中断处理（IRQ1）

**预期的困难：**
- 中断优先级和屏蔽
- 从中断返回时的栈平衡
- 处理嵌套中断

**当前进度：** ⏳ 即将开始

---

### 🔷 第四阶段：内存是怎么管理的（第 8-11 周）

#### 📝 第 6-8 周目标：理解虚拟内存和分页

**学习内容（计划）：**
- [ ] 虚拟地址与物理地址的映射
- [ ] 页表结构
- [ ] CR3 寄存器和页目录
- [ ] 缺页异常（#PF）
- [ ] 堆和栈的布局

**计划实现：**
- [ ] 页目录和页表
- [ ] 简单的页表映射
- [ ] 缺页异常处理
- [ ] 堆内存分配器（malloc/free）

**预期的困难：**
- 地址转换的多级索引
- 异常处理和内存保护
- 堆碎片管理

**当前进度：** ⏳ 即将开始

---

### 🔷 第五阶段：多进程和调度（第 12-15 周）

#### 📝 第 9-11 周目标：让系统能管理多个进程

**学习内容（计划）：**
- [ ] PCB（进程控制块）结构
- [ ] 进程创建和销毁
- [ ] 上下文切换
- [ ] 调度算法（轮转、优先级）
- [ ] 进程通信和同步

**计划实现：**
- [ ] PCB 数据结构
- [ ] 进程队列管理
- [ ] 上下文切换代码
- [ ] 基本的调度器

**预期的困难：**
- 栈的保存和恢复
- 处理进程的独立内存空间
- 调度算法的正确性

**当前进度：** ⏳ 即将开始

---

## 💭 学习过程中的反思

### 问题与解决方案日志

这一部分记录在实现过程中遇到的问题，以及如何解决它们。

#### 例子 1：汇编代码无法启动（预期会遇到）

**问题描述：**
编译好的 boot.bin 放入虚拟机，但屏幕一片黑，什么都没有。

**排查过程：**
1. 确认文件大小是否为 512 字节
2. 确认是否有 0xAA55 结束标志
3. 用 hexdump 查看生成的二进制
4. 在 QEMU 中启用调试输出

**解决方案：**
通常这类问题来自：
- 代码长度超过 512 字节
- 缺少或错误的启动标志
- 寄存器初始化不正确
- 中断启用时机不对

**学到的：**
- 对二进制文件的理解要清晰
- 调试的第一步总是"检查基本假设"

---

#### 例子 2：进入保护模式后无法执行代码（预期会遇到）

**问题描述：**
代码成功设置了 GDT 和 CR0，但在执行长跳转后程序就停止了。

**排查过程：**
1. 检查 GDT 的段描述符是否正确
2. 检查段选择子的值
3. 在 GDB 中设置断点，观察 CPU 状态
4. 检查是否缺少 GDTR（GDT 寄存器）的设置

**解决方案：**
通常是以下原因：
- GDT 中的代码段描述符权限位不对
- 长跳转的目标地址计算错误
- 进入保护模式后栈无效

**学到的：**
- GDT 不只是数据结构，它直接影响 CPU 如何执行代码
- 调试器很关键，能直接看到 CPU 寄存器变化

---

### "顿悟时刻"记录

这部分记录每个阶段中那些让理解突然豁然开朗的时刻。

#### 顿悟 1：为什么启动地址是 0x7c00？（预期在第 1 周）

**之前的理解：**
"0x7c00 是个魔数，就是这样规定的。"

**顿悟的过程：**
当我实现第一个引导程序时，写出 `[org 0x7c00]` 后，才真正理解：
- BIOS 加载 MBR 后，会跳转到 0x7c00 执行
- 这个地址是经过计算的（512KB - 512字节）
- 后面的代码和数据会接着放置

这不是"强行规定"，而是"有理由的设计"。

**收获：**
开始从设计者的角度思考"为什么这样做"，而不只是"怎么使用"。

---

#### 顿悟 2：中断就是硬件的"函数调用"（预期在第 3 周）

**之前的理解：**
"中断是个复杂的东西，涉及 IDT、栈、返回..."

**顿悟的过程：**
实现了第一个键盘中断处理程序后：
```nasm
keyboard_handler:
    push eax
    mov al, 0x20      ; EOI
    out 0x20, al
    pop eax
    iret              ; 从中断返回
```

我突然意识到：
- `int` 和 `iret` 就像 `call` 和 `ret`
- 不同的是，`int` 可以来自硬件
- CPU 通过 IDT 查表，就像查找函数地址
- 本质是相同的机制

**收获：**
中断不再是黑魔法，而是硬件和软件通信的优雅设计。

---

#### 顿悟 3：页表的分层设计（预期在第 6 周）

**之前的理解：**
"虚拟内存涉及页表映射，要用到 CR3 和一堆复杂的结构..."

**顿悟的过程：**
当我实现第一个页表时，计算地址翻译：
- 虚拟地址的高 10 位 → 查页目录
- 中间 10 位 → 查页表
- 低 12 位 → 页内偏移

我突然看到了分层设计的妙处：
- 不是一个巨大的表（需要 4MB 连续内存）
- 而是树状结构（需要的表可以动态分配）
- 通过 CR3 指向页目录，就能隔离每个进程的虚拟空间

**收获：**
理解了为什么操作系统要这样设计内存管理，而不是其他方式。

---

## 📊 学习深度的自我评估

### 第 1 周后（预期）

**理解程度：**
- [ ] 能解释计算机启动的每一步
- [ ] 能手写一个简单的 MBR
- [ ] 理解实模式寻址
- [ ] 理解 BIOS 中断基础

**代码能力：**
- [ ] 能写简单的汇编代码
- [ ] 能用 NASM 编译和链接
- [ ] 能用 QEMU 运行和测试

**自评：** ⭐⭐⭐ （理解基础概念）

---

### 第 4 周后（预期）

**理解程度：**
- [ ] 完全理解实模式和保护模式的区别
- [ ] 能解释 GDT 的每一个字段
- [ ] 理解为什么需要保护模式
- [ ] 能手写 GDT 和段描述符

**代码能力：**
- [ ] 能实现完整的模式切换
- [ ] 能用 GDB 调试汇编代码
- [ ] 理解栈和函数调用约定

**自评：** ⭐⭐⭐⭐ （深入理解核心概念）

---

### 第 8 周后（预期）

**理解程度：**
- [ ] 理解中断驱动的系统设计
- [ ] 理解虚拟内存的完整图景
- [ ] 能解释页表、TLB、缺页异常
- [ ] 理解进程的概念和隔离

**代码能力：**
- [ ] 能实现完整的中断处理系统
- [ ] 能实现分页机制
- [ ] 能调试多进程系统

**自评：** ⭐⭐⭐⭐⭐ （系统级理解）

---

## 🎓 知识框架的构建

### 启动前的理解
```
操作系统
├── 进程
├── 内存
├── 中断
└── 调度
    (每个都是独立的概念，不知道怎么串起来)
```

### 第 1 阶段后
```
计算机启动
└── BIOS → 加载 MBR → 跳转到代码
    (明白了第一步，但还不知道接下来是什么)
```

### 第 2 阶段后
```
引导加载器
└── 实模式
    └── 加载 GDT
        └── 切换到保护模式
            └── 运行 32 位代码
                (开始看到系统的升级过程)
```

### 第 3 阶段后
```
保护模式
├── 中断处理
│   ├── 硬件中断（键盘、时钟）
│   └── 异常（缺页、除以零）
└── 系统事件驱动
    (中断成为整个系统的脉搏)
```

### 第 4 阶段后
```
内存管理
├── 实模式（1MB，分段）
├── 保护模式（4GB，分页）
│   ├── 虚拟地址
│   ├── 页表转换
│   ├── 缺页异常
│   └── 内存隔离
└── 每个进程独立的虚拟空间
    (抽象的妙处显现)
```

### 最终（第 5 阶段后）
```
完整的操作系统
├── 启动过程（Bootloader）
├── 内存模型（段+页）
├── 中断系统（IDT）
├── 进程管理（PCB、调度）
├── 内存隔离（虚拟内存）
└── 驱动和 I/O
    (一切因果关系清晰)
```

---

## 📈 技能进阶路线图

```
第 1 阶段       第 2 阶段       第 3 阶段       第 4 阶段       第 5 阶段
┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
│ 汇编基础 │ → │ 保护模式│ → │ 中断处理│ → │ 内存管理│ → │ 进程调度│
│ 启动理解 │   │ 特权级  │   │ 中断向量│   │ 虚拟内存│   │ 上下文切│
│ 磁盘 I/O │   │ 段描述符│   │ IRQ/ISR│   │ 页表机制│   │ 时间片  │
└─────────┘   └─────────┘   └─────────┘   └─────────┘   └─────────┘
     ↓              ↓              ↓              ↓              ↓
   困难度 1.5    困难度 2.5    困难度 3.5    困难度 4.5    困难度 5
```

---

## 🎯 最终目标 vs 当前进度

### 短期目标（3 个月内）

- [ ] 完成可启动的引导加载器
- [ ] 成功切换到保护模式
- [ ] 实现基本的中断处理
- [ ] 实现简单的内存管理

**预期达成时间**：12 月中旬

### 中期目标（6 个月内）

- [ ] 完成多进程系统
- [ ] 实现进程调度
- [ ] 基础驱动程序
- [ ] 简单的 Shell

**预期达成时间**：2027 年 3 月

### 长期目标（1 年内）

- [ ] 文件系统实现
- [ ] 网络支持（可选）
- [ ] 完整的文档
- [ ] 详细的教学材料

**预期达成时间**：2027 年 10 月

---

## 💡 给未来的自己的建议

当你回顾这份日志时，记住：

1. **不要放弃**
   - 一定会遇到莫名其妙的 bug
   - 一定会有"为什么就是跑不起来"的时刻
   - 但这正是最大的学习机会

2. **记录问题和解决方案**
   - 花 5 分钟记录会为你节省 1 小时未来的重复查找
   - 这个日志就是为此而建

3. **定期回顾**
   - 每周看一遍这份日志
   - 对比你之前的理解
   - 你会看到自己在飞速成长

4. **分享知识**
   - 遇到问题，写到这份日志里
   - 别人遇到同样的问题时，会感谢你
   - 你将帮助他们节省数小时的调试时间

5. **享受过程**
   - 这不是为了完成一个项目
   - 这是为了理解计算机的本质
   - 每一个"顿悟时刻"都值得庆祝

---

## 📞 联系方式

如果你在学习过程中有疑问，欢迎：

- 在 GitHub Issues 提问
- 发邮件至 li228459909@163.com
- 发邮件至 lipeter10088001@gmail.com

记住：**你遇到的问题，很可能别人也遇到过。分享会让我们都进步。**

---

**开始你的系统编程之旅吧！ 🚀**

每一行代码都是对底层世界的理解。

---

---

# English

## 📖 Project Learning Journal

This is a record of the learning journey from scratch in building an operating system. It documents each step, problems encountered, insights gained, and project milestones.

This is not a technical manual. It is a **learner's journal**—tracking understanding depth, encountered challenges, solutions, and "aha" moments.

---

## 🎯 Why Document the Learning Process?

Many people focus only on the final product, ignoring the value of the learning journey itself. For such a complex system project, documenting the process matters because:

1. **Looking back, you can see how much you've grown**
2. **It helps future learners face the same challenges**
3. **It captures the "aha" moments**
4. **It contrasts before-and-after understanding**

---

## 📅 Project Milestone Timeline

### 🔷 Phase 1: The Bootloader World (Week 1-2)

#### ✅ 2026-10-11 - Project Initialization

**What was done:**
- Created os-from-scratch repository on GitHub
- Established project structure
- Wrote comprehensive bilingual README
- Prepared development environment

**What I learned:**
- GitHub repository best practices
- Importance of project documentation
- Open-source code style standards

**Challenges faced:**
- None (this was setup phase)

**Feelings at this point:**
🎉 Excited. Ready to really start this journey.

---

#### 📝 Week 1 Target: Understand Computer Boot Process

**Learning topics:**
- [ ] x86 real mode addressing
- [ ] BIOS boot flow (POST → bootloader)
- [ ] MBR structure
- [ ] Meaning of 0x7c00
- [ ] Using int 0x13 (disk read)

**Implementation plan:**
- [ ] Minimal MBR code
- [ ] Disk read function
- [ ] String print function
- [ ] Kernel loading and jumping

**Study materials:**
- OSDev.org Bootloader Tutorial
- Bran's Kernel Development Tutorial Chapter 1
- x86 Assembly Basics

**Expected difficulties:**
- Assembly syntax unfamiliarity
- Binary layout and address calculation
- Debugging low-level code

**Current progress:** 🔄 In progress

---

### 🔷 Phase 2: Protected Mode (Week 3-4)

#### 📝 Week 2-3 Target: Enter the 32-bit World

**Learning topics (planned):**
- [ ] GDT structure
- [ ] Segment descriptors and privilege levels
- [ ] CR0 register and PE bit
- [ ] Long jump instruction (jmp far)
- [ ] 32-bit registers and addressing

**Implementation plan:**
- [ ] GDT data structure
- [ ] GDT loading instruction (lgdt)
- [ ] Protected mode switch code
- [ ] 32-bit hello world

**Expected difficulties:**
- GDT byte meanings
- Segment selector calculations
- The leap from real to protected mode

**Current progress:** ⏳ Coming soon

---

### 🔷 Phase 3: Interrupt-Driven System (Week 5-7)

#### 📝 Week 4-5 Target: Let System Respond to Hardware Events

**Learning topics (planned):**
- [ ] IDT structure
- [ ] Interrupt vs Exception
- [ ] ISR (Interrupt Service Routine)
- [ ] 8259 PIC
- [ ] IRQ to vector mapping

**Implementation plan:**
- [ ] IDT data structure
- [ ] Interrupt handler framework
- [ ] Timer interrupt (IRQ0)
- [ ] Keyboard interrupt (IRQ1)

**Expected difficulties:**
- Interrupt priority and masking
- Stack balance on interrupt return
- Nested interrupt handling

**Current progress:** ⏳ Coming soon

---

### 🔷 Phase 4: Memory Management (Week 8-11)

#### 📝 Week 6-8 Target: Virtual Memory and Paging

**Learning topics (planned):**
- [ ] Virtual to physical address mapping
- [ ] Page table structure
- [ ] CR3 register and page directory
- [ ] Page fault (#PF)
- [ ] Heap and stack layout

**Implementation plan:**
- [ ] Page directory and page tables
- [ ] Basic page table mapping
- [ ] Page fault exception handler
- [ ] Heap allocator (malloc/free)

**Expected difficulties:**
- Multi-level address translation
- Exception handling and memory protection
- Heap fragmentation management

**Current progress:** ⏳ Coming soon

---

### 🔷 Phase 5: Multiprocessing and Scheduling (Week 12-15)

#### 📝 Week 9-11 Target: Manage Multiple Processes

**Learning topics (planned):**
- [ ] PCB structure
- [ ] Process creation and termination
- [ ] Context switching
- [ ] Scheduling algorithms
- [ ] IPC and synchronization

**Implementation plan:**
- [ ] PCB data structure
- [ ] Process queue management
- [ ] Context switch code
- [ ] Basic scheduler

**Expected difficulties:**
- Saving and restoring stacks
- Independent memory spaces
- Scheduler correctness

**Current progress:** ⏳ Coming soon

---

## 💭 Reflections During Learning

### Problem & Solution Log

#### Example 1: Assembly Code Won't Boot (Expected issue)

**Problem:**
Compiled boot.bin placed in VM shows black screen.

**Investigation:**
1. Check if file size is 512 bytes
2. Check for 0xAA55 signature
3. Use hexdump to inspect binary
4. Enable debug output in QEMU

**Solution:**
Usually caused by:
- Code exceeds 512 bytes
- Missing or wrong boot signature
- Incorrect register initialization
- Wrong interrupt timing

**Learned:**
- Need clear understanding of binary files
- Debugging starts with checking basic assumptions

---

#### Example 2: Can't Execute After Entering Protected Mode (Expected issue)

**Problem:**
Code sets GDT and CR0, but stops after long jump.

**Investigation:**
1. Check GDT segment descriptor validity
2. Check segment selector value
3. Set breakpoints in GDB, observe CPU state
4. Check if GDTR is properly initialized

**Solution:**
Usually caused by:
- Wrong permission bits in code segment descriptor
- Incorrect target address in long jump
- Invalid stack after mode switch

**Learned:**
- GDT directly affects code execution
- Debugger is crucial for seeing register changes

---

### "Aha" Moments Log

#### Aha Moment 1: Why Boot Address is 0x7c00? (Expected week 1)

**Previous understanding:**
"0x7c00 is a magic number, it's just defined that way."

**The awakening:**
When implementing the first bootloader with `[org 0x7c00]`:
- BIOS loads MBR then jumps to 0x7c00
- This address is calculated (512KB - 512 bytes)
- Code and data follow sequentially

It's not arbitrary—it's a reasoned design.

**Takeaway:**
Started thinking like a designer, not just a user.

---

#### Aha Moment 2: Interrupts are Hardware's "Function Calls" (Expected week 3)

**Previous understanding:**
"Interrupts are complex, involving IDT, stack, return..."

**The awakening:**
After implementing first keyboard handler:
```nasm
keyboard_handler:
    push eax
    mov al, 0x20      ; EOI
    out 0x20, al
    pop eax
    iret              ; return from interrupt
```

Suddenly realized:
- `int` and `iret` work like `call` and `ret`
- Difference: `int` can come from hardware
- CPU looks up IDT like function addresses
- Same underlying mechanism

**Takeaway:**
Interrupts are elegant hardware-software communication, not magic.

---

#### Aha Moment 3: Hierarchical Page Table Design (Expected week 6)

**Previous understanding:**
"Virtual memory involves page table mapping with CR3..."

**The awakening:**
Implementing first page table, calculating translation:
- Virtual address bits 22-31 → page directory
- Bits 12-21 → page table
- Bits 0-11 → page offset

Suddenly saw the design beauty:
- Not one giant table (need 4MB continuous memory)
- But tree structure (tables allocated dynamically)
- CR3 points to page directory, isolating each process

**Takeaway:**
Understood why OS uses this design, not alternatives.

---

## 📊 Self-Assessment of Learning Depth

### After Week 1 (Expected)

**Understanding level:**
- [ ] Can explain each boot step
- [ ] Can write simple MBR
- [ ] Understand real mode addressing
- [ ] Understand BIOS interrupt basics

**Code ability:**
- [ ] Write basic assembly
- [ ] Compile/link with NASM
- [ ] Run and test with QEMU

**Self-rating:** ⭐⭐⭐ (Basic concepts)

---

### After Week 4 (Expected)

**Understanding level:**
- [ ] Fully understand real vs protected mode
- [ ] Explain every GDT field
- [ ] Understand why protected mode exists
- [ ] Write GDT and descriptors

**Code ability:**
- [ ] Implement complete mode switch
- [ ] Debug assembly with GDB
- [ ] Understand stack and calling conventions

**Self-rating:** ⭐⭐⭐⭐ (Deep concept understanding)

---

### After Week 8 (Expected)

**Understanding level:**
- [ ] Understand interrupt-driven design
- [ ] Complete virtual memory picture
- [ ] Explain paging, TLB, page faults
- [ ] Understand process isolation

**Code ability:**
- [ ] Implement complete interrupt system
- [ ] Implement paging mechanism
- [ ] Debug multiprocess system

**Self-rating:** ⭐⭐⭐⭐⭐ (System-level understanding)

---

## 🎓 Building a Knowledge Framework

### Before starting
```
Operating System
├── Process
├── Memory
├── Interrupt
└── Scheduling
    (Independent concepts, don't know how to connect)
```

### After Phase 1
```
Computer Boot
└── BIOS → Load MBR → Jump to code
    (Understand first step, unsure what's next)
```

### After Phase 2
```
Bootloader
└── Real Mode
    └── Load GDT
        └── Switch to Protected Mode
            └── Run 32-bit code
                (Start seeing system evolution)
```

### After Phase 3
```
Protected Mode
├── Interrupt Handling
│   ├── Hardware (keyboard, timer)
│   └── Exceptions (page fault, divide-by-zero)
└── Event-Driven System
    (Interrupts are the system's heartbeat)
```

### After Phase 4
```
Memory Management
├── Real Mode (1MB, segmented)
├── Protected Mode (4GB, paged)
│   ├── Virtual address
│   ├── Page table translation
│   ├── Page fault
│   └── Memory isolation
└── Each process has independent virtual space
    (The elegance becomes clear)
```

### Final (After Phase 5)
```
Complete Operating System
├── Boot Process (Bootloader)
├── Memory Model (segment+paging)
├── Interrupt System (IDT)
├── Process Management (PCB, scheduling)
├── Memory Isolation (virtual memory)
└── Drivers and I/O
    (All causality is clear)
```

---

## 📈 Skill Progression Roadmap

```
Phase 1         Phase 2         Phase 3         Phase 4         Phase 5
┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
│Assembly │ → │Protected│ → │Interrupt│ → │ Memory  │ → │ Process │
│ Boot    │   │ Mode    │   │ Handler │   │Management   │Scheduling
│Disk I/O │   │Privilege   │ IRQ/ISR │   │ Paging  │   │Context  │
└─────────┘   └─────────┘   └─────────┘   └─────────┘   └─────────┘
     ↓              ↓              ↓              ↓              ↓
  Difficulty 1.5  2.5          3.5          4.5          5.0
```

---

## 🎯 Goals vs Current Progress

### Short-term (3 months)

- [ ] Bootable bootloader
- [ ] Successful protected mode switch
- [ ] Basic interrupt handling
- [ ] Simple memory management

**Target completion:** Mid-December

### Medium-term (6 months)

- [ ] Multiprocess system
- [ ] Process scheduling
- [ ] Basic drivers
- [ ] Simple Shell

**Target completion:** March 2027

### Long-term (1 year)

- [ ] File system
- [ ] Network support (optional)
- [ ] Complete documentation
- [ ] Detailed learning materials

**Target completion:** October 2027

---

## 💡 Advice to Future Self

When reviewing this journal, remember:

1. **Don't give up**
   - You'll definitely encounter mysterious bugs
   - There will be "why won't this run?" moments
   - These are the best learning opportunities

2. **Document problems and solutions**
   - 5 minutes of documentation saves 1 hour later
   - This journal exists for this purpose

3. **Review regularly**
   - Read this weekly
   - Compare your old understanding
   - You'll see your rapid growth

4. **Share knowledge**
   - Write problems here
   - Others will thank you for solutions
   - You'll save them hours of debugging

5. **Enjoy the process**
   - This isn't just project completion
   - This is understanding computer fundamentals
   - Celebrate every "aha" moment

---

## 📞 Contact

If you have questions during learning:

- Open GitHub Issues
- Email: li228459909@163.com
- Email: lipeter10088001@gmail.com

Remember: **Problems you face, others have faced. Sharing helps everyone grow.**

---

**Start your systems programming journey! 🚀**

Every line of code is understanding the low-level world.
