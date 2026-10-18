# LittleOS 教学操作系统学习笔记

> 一个从零开始，通过亲手编写一个简单教学操作系统，学习 x86 汇编、C 语言、操作系统和计算机底层原理的长期学习工程。

---

# 目录

1. [LittleOS 项目目标](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#1-littleos-%E9%A1%B9%E7%9B%AE%E7%9B%AE%E6%A0%87)
    
2. [学习环境](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#2-%E5%AD%A6%E4%B9%A0%E7%8E%AF%E5%A2%83)
    
3. [学习方法](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#3-%E5%AD%A6%E4%B9%A0%E6%96%B9%E6%B3%95)
    
4. [第一阶段：第一个启动扇区](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#4-%E7%AC%AC%E4%B8%80%E9%98%B6%E6%AE%B5%E7%AC%AC%E4%B8%80%E4%B8%AA%E5%90%AF%E5%8A%A8%E6%89%87%E5%8C%BA)
    
5. [启动扇区为什么必须是 512 字节](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#5-%E5%90%AF%E5%8A%A8%E6%89%87%E5%8C%BA%E4%B8%BA%E4%BB%80%E4%B9%88%E5%BF%85%E9%A1%BB%E6%98%AF-512-%E5%AD%97%E8%8A%82)
    
6. [55 AA 启动签名](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#6-55-aa-%E5%90%AF%E5%8A%A8%E7%AD%BE%E5%90%8D)
    
7. [QEMU 与 BIOS 启动过程](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#7-qemu-%E4%B8%8E-bios-%E5%90%AF%E5%8A%A8%E8%BF%87%E7%A8%8B)
    
8. [第二阶段：从汇编源代码到机器码](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#8-%E7%AC%AC%E4%BA%8C%E9%98%B6%E6%AE%B5%E4%BB%8E%E6%B1%87%E7%BC%96%E6%BA%90%E4%BB%A3%E7%A0%81%E5%88%B0%E6%9C%BA%E5%99%A8%E7%A0%81)
    
9. [NASM 是什么](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#9-nasm-%E6%98%AF%E4%BB%80%E4%B9%88)
    
10. [NASM 到底做了什么](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#10-nasm-%E5%88%B0%E5%BA%95%E5%81%9A%E4%BA%86%E4%BB%80%E4%B9%88)
    
11. [机器指令、伪指令和汇编器指令](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#11-%E6%9C%BA%E5%99%A8%E6%8C%87%E4%BB%A4%E4%BC%AA%E6%8C%87%E4%BB%A4%E5%92%8C%E6%B1%87%E7%BC%96%E5%99%A8%E6%8C%87%E4%BB%A4)
    
12. [BITS、ORG、标签、DB、DW、TIMES](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#12-bitsorg%E6%A0%87%E7%AD%BEdbdwtimes)
    
13. [CPU 为什么能够执行机器码](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#13-cpu-%E4%B8%BA%E4%BB%80%E4%B9%88%E8%83%BD%E5%A4%9F%E6%89%A7%E8%A1%8C%E6%9C%BA%E5%99%A8%E7%A0%81)
    
14. [反汇编：ndisasm](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#14-%E5%8F%8D%E6%B1%87%E7%BC%96ndisasm)
    
15. [寄存器与 GDB 调试实验](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#15-%E5%AF%84%E5%AD%98%E5%99%A8%E4%B8%8E-gdb-%E8%B0%83%E8%AF%95%E5%AE%9E%E9%AA%8C)
    
16. [第三阶段：自己写 Mini Assembler](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#16-%E7%AC%AC%E4%B8%89%E9%98%B6%E6%AE%B5%E8%87%AA%E5%B7%B1%E5%86%99-mini-assembler)
    
17. [Mini Assembler 第一次成功](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#17-mini-assembler-%E7%AC%AC%E4%B8%80%E6%AC%A1%E6%88%90%E5%8A%9F)
    
18. [标签与地址计算](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#18-%E6%A0%87%E7%AD%BE%E4%B8%8E%E5%9C%B0%E5%9D%80%E8%AE%A1%E7%AE%97)
    
19. [相对跳转 JMP](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#19-%E7%9B%B8%E5%AF%B9%E8%B7%B3%E8%BD%AC-jmp)
    
20. [LittleOS 显示字母 A](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#20-littleos-%E6%98%BE%E7%A4%BA%E5%AD%97%E6%AF%8D-a)
    
21. [字符串、LODSB 和 DS](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#21-%E5%AD%97%E7%AC%A6%E4%B8%B2lodsb-%E5%92%8C-dssi)
    
22. [TEST AL, AL 与 JZ](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#22-test-al-al-%E4%B8%8E-jz)
    
23. [CMP、JE、JNE](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#23-cmpjejne)
    
24. [当前 Mini Assembler 的能力](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#24-%E5%BD%93%E5%89%8D-mini-assembler-%E7%9A%84%E8%83%BD%E5%8A%9B)
    
25. [当前已经掌握的知识体系](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#25-%E5%BD%93%E5%89%8D%E5%B7%B2%E7%BB%8F%E6%8E%8C%E6%8F%A1%E7%9A%84%E7%9F%A5%E8%AF%86%E4%BD%93%E7%B3%BB)
    
26. [曾经遇到的问题与排查](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#26-%E6%9B%BE%E7%BB%8F%E9%81%87%E5%88%B0%E7%9A%84%E9%97%AE%E9%A2%98%E4%B8%8E%E6%8E%92%E6%9F%A5)
    
27. [目前 LittleOS 的状态](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#27-%E7%9B%AE%E5%89%8D-littleos-%E7%9A%84%E7%8A%B6%E6%80%81)
    
28. [下一阶段学习路线](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#28-%E4%B8%8B%E4%B8%80%E9%98%B6%E6%AE%B5%E5%AD%A6%E4%B9%A0%E8%B7%AF%E7%BA%BF)
    
29. [复习题](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#29-%E5%A4%8D%E4%B9%A0%E9%A2%98)
    
30. [学习原则](https://chatgpt.com/c/6ace4113-b408-83e8-822d-8d492aa5f02a#30-%E5%AD%A6%E4%B9%A0%E5%8E%9F%E5%88%99)
    

---

# 1. LittleOS 项目目标

## 1.1 为什么做 LittleOS

这个项目不是为了做一个真正能够与 Linux、Windows 竞争的操作系统。

目标是：

> **通过亲手写一个非常小的操作系统，理解计算机从“通电”到“执行程序”之间究竟发生了什么。**

重点学习：

- x86 CPU
    
- x86 汇编语言
    
- C 语言
    
- 机器码
    
- 寄存器
    
- 内存
    
- CPU 指令
    
- FLAGS
    
- 跳转
    
- BIOS
    
- 中断
    
- 启动过程
    
- Boot Sector
    
- Bootloader
    
- 内核
    
- 内存管理
    
- 键盘输入
    
- 屏幕输出
    
- C 与汇编如何配合
    
- 编译器
    
- 链接器
    
- ELF
    
- CPU 保护模式
    
- 32 位/64 位模式
    
- 最终逐渐理解现代操作系统的基本结构
    

---

# 2. 学习环境

当前开发环境：

```
Arch Linux
+
Hyprland
+
x86 PC architecture
+
NASM
+
QEMU
+
GDB
+
Python
```

LittleOS 项目目录：

```
~/projects/littleos
```

Git 分支目前使用：

```
master
```

---

# 3. 学习方法

整个项目采用：

> **先讲原理，再写代码，再观察机器码，再验证 CPU 行为。**

不追求一次学很多。

例如学习：

```
mov al, 0x41
```

不是只记住：

```
B0 41
```

而是理解：

```
汇编：
    mov al, 0x41

        ↓

汇编器：

    B0 41

        ↓

机器码

        ↓

CPU 取指

        ↓

CPU 识别 B0

        ↓

知道这是 MOV AL, imm8

        ↓

AL = 41h
```

以后学习 C 语言、编译器和操作系统时，也继续采用这种思路。

---

# 4. 第一阶段：第一个启动扇区

最开始，我们完成了 LittleOS 的第一个实验：

> 创建一个真正可以被 BIOS 当作启动盘读取的 512 字节启动扇区。

实验最终成功。

QEMU 启动后能够看到：

```
Hello from LittleOS!
```

这意味着：

```
源代码
 ↓
NASM
 ↓
机器码
 ↓
512 字节 Boot Sector
 ↓
QEMU 模拟硬件
 ↓
BIOS
 ↓
加载 Boot Sector
 ↓
CPU 执行
 ↓
屏幕输出
```

这是 LittleOS 的第一个真正里程碑。

---

# 5. 启动扇区为什么必须是 512 字节

传统 BIOS 启动过程中，一个基本磁盘扇区大小通常是：

```
512 bytes
```

因此我们的 Boot Sector 必须满足：

```
总大小 = 512 字节
```

之前使用：

```
wc -c mini_boot.bin
```

验证：

```
512
```

---

# 6. 55 AA 启动签名

启动扇区最后两个字节必须是：

```
55 AA
```

也就是：

```
offset 510 = 55
offset 511 = AA
```

因此：

```
前 510 字节
+
55 AA
=
512 字节
```

验证命令：

```
od -An -tx1 -j 510 -N 2 mini_boot.bin
```

结果：

```
55 aa
```

这说明：

```
Boot Sector
    ↓
512 bytes
    ↓
最后两个字节
    ↓
55 AA
```

---

# 7. QEMU 与 BIOS 启动过程

使用 QEMU 模拟计算机。

启动后曾经看到：

```
Booting from floppy......
```

这意味着模拟的 BIOS 正在尝试从软盘设备启动。

简化过程：

```
QEMU
 ↓
模拟 x86 CPU
 ↓
模拟 BIOS
 ↓
BIOS 读取启动设备
 ↓
读取第一个扇区
 ↓
检查 55 AA
 ↓
把 Boot Sector 放入内存
 ↓
CPU 开始执行
```

传统 BIOS 会把 Boot Sector 放到：

```
0x7C00
```

附近。

这也是后来我们使用：

```
ORG 0x7C00
```

的重要原因。

---

# 8. 第二阶段：从汇编源代码到机器码

我们开始研究：

> 汇编语言程序究竟是如何变成 CPU 能够执行的机器码的？

例如：

```
mov ax, 0x04D2
hlt
db 'OK', 0
dw 0xAA55
```

NASM：

```
nasm -f bin data.asm -o data.bin
```

查看：

```
od -An -tx1 data.bin
```

得到：

```
b8 d2 04 f4 4f 4b 00 55 aa
```

文件大小：

```
wc -c data.bin
```

结果：

```
9
```

---

# 9. NASM 是什么

NASM：

```
Netwide Assembler
```

是一个汇编器。

它不是 CPU。

它也不是机器码。

它运行在 Linux 上，负责：

> **把汇编源代码翻译成机器码。**

例如：

```
mov al, 0x41
```

经过 NASM：

```
B0 41
```

---

# 10. NASM 到底做了什么

可以把 NASM 理解成一个“翻译器”。

输入：

```
mov al, 0x41
```

NASM 解析：

```
mov
al
0x41
```

然后知道：

```
MOV AL, imm8
```

对应的 opcode：

```
B0
```

立即数：

```
41
```

最终生成：

```
B0 41
```

---

# 11. 机器指令、伪指令和汇编器指令

这是之前重点学习的内容。

## 11.1 真正的 CPU 指令

例如：

```
mov
hlt
jmp
int
lodsb
test
cmp
```

这些最终都有对应的机器码。

例如：

```
mov al, 0x41
```

↓

```
B0 41
```

---

## 11.2 伪指令 / 汇编器指令

例如：

```
BITS 16
ORG 0x7C00
DB
DW
TIMES
```

它们并不一定对应 CPU 指令。

例如：

```
BITS 16
```

是在告诉 NASM：

> 接下来按照 16 位代码模式进行汇编。

CPU 并不会执行：

```
BITS 16
```

---

## 11.3 ORG

例如：

```
ORG 0x7C00
```

告诉汇编器：

> 假设这段程序最终会被放在内存地址 0x7C00。

它主要影响：

- 标签地址计算
    
- 立即数
    
- 跳转目标
    
- 数据地址
    

它本身不是 CPU 指令。

---

# 12. BITS、ORG、标签、DB、DW、TIMES

## 12.1 BITS 16

```
BITS 16
```

告诉 NASM：

```
按照 16 位 x86 模式生成代码
```

---

## 12.2 ORG

```
ORG 0x7C00
```

表示：

```
程序加载地址假设为 0x7C00
```

---

## 12.3 标签

例如：

```
start:
```

标签本身通常不会产生机器码。

它代表：

```
当前位置的地址
```

例如：

```
start:
    mov al, 'A'
```

如果 `start` 对应地址：

```
7C00
```

那么 NASM 会记住：

```
start = 0x7C00
```

---

## 12.4 DB

```
db 'A', 0
```

表示直接放入字节：

```
41 00
```

所以：

```
DB
```

不是 CPU 指令。

它是在告诉汇编器：

> 把这些数据直接放进输出文件。

---

## 12.5 DW

```
dw 0xAA55
```

表示定义一个 word。

16 位：

```
2 bytes
```

在 x86 的小端序表示下：

```
AA55
```

会保存成：

```
55 AA
```

---

## 12.6 TIMES

例如：

```
times 510-($-$$) db 0
```

可以用来填充剩余空间。

Boot Sector：

```
代码
+
填充
+
55 AA
=
512 bytes
```

---

# 13. CPU 为什么能够执行机器码

CPU 不认识：

```
mov al, 0x41
```

CPU 真正看到的是：

```
B0 41
```

CPU 读取内存：

```
B0
```

CPU 内部的指令译码逻辑识别：

```
B0 = MOV AL, imm8
```

然后继续读取：

```
41
```

于是执行：

```
AL = 0x41
```

可以简单理解成：

```
内存
 ↓
取指
 ↓
读取 opcode
 ↓
译码
 ↓
执行
 ↓
修改寄存器/内存/FLAGS/IP
 ↓
下一条指令
```

这就是 CPU 最基本的：

```
Fetch
Decode
Execute
```

过程。

---

# 14. 反汇编：ndisasm

NASM：

```
汇编代码 → 机器码
```

而：

```
ndisasm
```

可以：

```
机器码 → 汇编形式
```

例如：

```
ndisasm -b 16 hello.bin
```

曾经得到：

```
00000000  B80000            mov ax,0x0
00000003  8ED8              mov ds,ax
00000005  B041              mov al,0x41
00000007  B40E              mov ah,0xe
00000009  CD10              int byte 0x10
0000000B  F4                hlt
0000000C  EBFD              jmp 0xb
```

这让我们能够验证：

```
汇编代码
 ↕
机器码
```

之间的关系。

---

# 15. 寄存器与 GDB 调试实验

我们曾经创建：

```
register.asm
register.bin
```

其中：

```
register.asm
```

大小：

```
38 bytes
```

机器码：

```
6 bytes
```

---

## 15.1 为什么不能直接用 GDB 打开 register.bin

执行：

```
file register.bin
```

得到：

```
not in executable format: file format not recognized
```

原因：

```
register.bin
```

只是一个：

> 原始二进制文件。

它不是 ELF 可执行文件。

GDB 通常希望得到带有：

- 文件格式
    
- 符号
    
- 调试信息
    
- 段信息
    

等结构的可执行文件。

---

## 15.2 debug 实验

后来使用一个真正的可执行文件：

```
debug
```

在 GDB：

```
break _start
run
```

停在：

```
0x08049000 <_start>
```

然后：

```
info registers eax ebx eip
```

得到：

```
eax = 0
ebx = 0
eip = 0x08049000
```

这说明：

```
EIP
```

是当前执行位置。

单步：

```
si
```

可以看到：

```
0x08049005
0x08049007
0x08049008 in hang()
```

这让我们第一次实际观察到了：

> CPU 执行一条指令后，EIP 如何变化。

---

# 16. 第三阶段：自己写 Mini Assembler

这是 LittleOS 项目非常重要的一个转折点。

我们不再只使用 NASM。

开始自己写：

```
mini_asm.py
```

目标：

> 写一个非常简化的汇编器，从而亲自理解 NASM 的基本工作原理。

---

# 17. Mini Assembler 第一次成功

第一次测试：

```
汇编程序：
mov ax, 0x04D2
hlt
db 'OK', 0
dw 0xAA55
```

Mini Assembler 输出：

```
B8 D2 04 F4 4F 4B 00 55 AA
```

和 NASM：

```
B8 D2 04 F4 4F 4B 00 55 AA
```

完全一致。

这证明我们自己的程序已经可以完成最基本的：

```
汇编代码
 ↓
解析
 ↓
生成机器码
```

---

# 18. 标签与地址计算

之后开始研究：

```
jmp label
```

为什么汇编器不能简单地看到：

```
jmp print
```

就立即生成机器码？

因为：

```
print
```

代表的是一个地址。

例如：

```
start:
    jmp print

    db 'ABC', 0

print:
    hlt
```

汇编器必须先知道：

```
start 在哪里
print 在哪里
```

因此我们学习了“两遍汇编”。

---

## 第一遍

只计算：

```
每条语句有多长
标签在哪里
```

例如：

```
start = 0x7C00
print = 0x7C06
```

---

## 第二遍

真正生成：

```
机器码
```

这就是为什么我们自己的 `mini_asm.py` 开始采用：

```
第一遍：
    计算标签地址

第二遍：
    生成机器码
```

这是理解真正汇编器工作方式的非常重要的一步。

---

# 19. 相对跳转 JMP

我们学习了：

```
jmp label
```

短跳转机器码：

```
EB xx
```

其中：

```
xx
```

不是绝对地址。

它是：

> **相对于下一条指令的偏移量。**

公式：

```
offset = target - next_instruction
```

例如：

```
当前 JMP：
7C04

JMP 长度：
2 bytes

下一条：
7C06

目标：
7C08
```

所以：

```
offset = 7C08 - 7C06
       = 2
```

最终：

```
EB 02
```

---

## 19.1 为什么会看到 EB FD

我们曾经使用：

```
hang:
    hlt
    jmp hang
```

假设：

```
hlt = 7C09
jmp = 7C0A
```

下一条：

```
7C0C
```

目标：

```
7C0A
```

所以：

```
7C0A - 7C0C = -2
```

8 位补码：

```
FE
```

如果跳转结构不同，例如：

```
jmp $
```

也经常会看到：

```
EB FE
```

我们 LittleOS 当前程序中使用过：

```
EB FD
```

其含义就是：

> 向后跳转，形成无限循环。

---

# 20. LittleOS 显示字母 A

经过 Mini Assembler 的多次调试，我们最终成功生成：

```
B0 41 B4 0E CD 10 F4 EB FD
```

即：

```
mov al, 'A'
mov ah, 0x0E
int 0x10
hlt
jmp $
```

QEMU：

```
Booting from floppy......
```

然后最终出现：

```
A
```

这是 LittleOS 的又一个重要里程碑。

---

# 21. 字符串、LODSB 和 DS

之后开始学习字符串。

例如：

```
message:
    db 'A', 0
```

机器码 / 数据：

```
41 00
```

其中：

```
41 = 'A'
00 = 字符串结束符
```

---

## 21.1 SI

我们使用：

```
mov si, message
```

让：

```
SI
```

指向字符串。

例如：

```
SI = 0x7C0E
```

---

## 21.2 LODSB

：

```
lodsb
```

机器码：

```
AC
```

作用可以简单理解为：

```
AL = [DS:SI]
SI = SI + 1
```

也就是说：

```
内存
 ↓
SI 指向的字符
 ↓
AL
```

例如：

```
message:
    41 00
```

第一次：

```
AL = 41
SI++
```

第二次：

```
AL = 00
SI++
```

---

# 22. TEST AL, AL 与 JZ

我们后来加入：

```
test al, al
jz hang
```

`TEST`：

```
test al, al
```

机器码：

```
84 C0
```

本质上计算：

```
AL AND AL
```

但是不保存计算结果。

它主要修改：

```
FLAGS
```

特别是：

```
ZF
```

如果：

```
AL = 0
```

那么：

```
AL AND AL = 0
```

所以：

```
ZF = 1
```

于是：

```
jz hang
```

发生跳转。

---

## 22.1 JZ

```
JZ
```

意思：

```
Jump if Zero
```

也就是：

```
如果 ZF = 1，就跳转。
```

机器码：

```
74 xx
```

---

# 23. CMP、JE、JNE

这是目前最后学习的一部分。

---

## 23.1 CMP

例如：

```
cmp al, 'A'
```

机器码：

```
3C 41
```

CPU 可以理解成：

```
AL - 41
```

但：

> **CMP 不会把减法结果保存回 AL。**

它主要用来设置 FLAGS。

---

## 23.2 相等

如果：

```
AL = 41
```

执行：

```
cmp al, 41h
```

CPU：

```
41 - 41 = 0
```

所以：

```
ZF = 1
```

然后：

```
je found
```

就会跳转。

---

## 23.3 不相等

如果：

```
AL = 42
```

那么：

```
42 - 41 = 1
```

因此：

```
ZF = 0
```

所以：

```
je found
```

不会跳。

---

## 23.4 JE 和 JZ

非常重要：

```
JE = JZ
```

两者使用相同的条件：

```
ZF = 1
```

机器码也是一样：

```
74 xx
```

区别主要是语义。

例如：

```
test al, al
jz zero
```

强调：

```
结果是不是 0
```

而：

```
cmp al, 'A'
je found
```

强调：

```
两个值是不是相等
```

CPU 最终看的仍然是：

```
ZF
```

---

## 23.5 JNE

：

```
jne not_found
```

意思：

```
Jump if Not Equal
```

条件：

```
ZF = 0
```

机器码：

```
75 xx
```

因此：

```
CMP + JE
```

可以对应 C：

```
if (a == b)
```

而：

```
CMP + JNE
```

可以对应：

```
if (a != b)
```

---

# 24. 当前 Mini Assembler 的能力

目前自己的：

```
mini_asm.py
```

已经可以理解并生成一部分 x86 指令。

当前学习版本包括：

```
BITS 16
ORG xxxx

MOV AX, imm16
MOV AL, imm8
MOV AH, imm8
MOV SI, imm16

LODSB

TEST AL, AL
CMP AL, imm8

JMP label
JZ label
JE label
JNE label

INT imm8
HLT

DB
```

---

## 当前重要机器码表

|汇编|机器码|
|---|---|
|`mov ax, imm16`|`B8 xx xx`|
|`mov al, imm8`|`B0 xx`|
|`mov ah, imm8`|`B4 xx`|
|`mov si, imm16`|`BE xx xx`|
|`lodsb`|`AC`|
|`test al, al`|`84 C0`|
|`cmp al, imm8`|`3C xx`|
|`jmp short`|`EB xx`|
|`jz`|`74 xx`|
|`je`|`74 xx`|
|`jne`|`75 xx`|
|`int imm8`|`CD xx`|
|`hlt`|`F4`|

---

# 25. 当前已经掌握的知识体系

到目前为止，LittleOS 已经从最初的：

```
Hello World
```

逐渐发展成：

```
汇编源代码
     ↓
NASM
     ↓
机器码
     ↓
Boot Sector
     ↓
BIOS
     ↓
0x7C00
     ↓
CPU
     ↓
寄存器
     ↓
内存
     ↓
FLAGS
     ↓
条件跳转
     ↓
程序控制流
```

---

# 26. 曾经遇到的问题与排查

## 26.1 GDB 无法直接打开 register.bin

错误：

```
not in executable format
```

原因：

```
register.bin
```

是裸二进制文件，不是 ELF。

---

## 26.2 Mini Assembler 缩进错误

曾经出现：

```
IndentationError
```

例如：

```
elif kind == "hlt":
```

缩进层级不一致。

这也是 Python 初学时非常典型的问题。

---

## 26.3 把 Markdown 代码围栏写进 Python

曾经出现：

```
SyntaxError
```

因为把：

````
```python
````

一起保存到了：

```
mini_asm.py
```

Python 不认识 Markdown 的代码围栏。

正确的 `.py` 文件应该直接从：

```
import ...
```

开始，而不是：

````
```python
````

---

## 26.4 Boot Sector 大小正确但 QEMU 黑屏

曾经出现：

```
512 bytes
55 AA
```

全部正确，但 QEMU 黑屏。

检查：

```
od -An -tx1 -N 12 mini_boot.bin
```

曾经得到：

```
b8 d2 04 f4 00 00 00 00 00 00 00 00
```

这说明：

> 虽然启动扇区格式正确，但程序本身生成错了。

最终修复 Mini Assembler 后得到：

```
B0 41 B4 0E CD 10 F4 EB FD
```

再次运行 QEMU：

```
A
```

成功。

这个过程非常重要，因为它让我们认识到：

> `55 AA` 正确 ≠ 程序一定正确。

Boot Sector 必须同时满足：

```
格式正确
+
机器码正确
+
CPU 能够正确执行
```

---

# 27. 目前 LittleOS 的状态

当前最后一个成功版本的核心程序可以理解为：

```
BITS 16
ORG 0x7C00

start:
    mov ax, 0
    mov ds, ax
    mov si, message

print:
    lodsb

    cmp al, 0
    je hang

    mov ah, 0x0E
    int 0x10

    jmp print

hang:
    hlt
    jmp hang

message:
    db 'A', 0
```

它的逻辑是：

```
启动
 ↓
DS = 0
 ↓
SI = message
 ↓
读取一个字符
 ↓
AL = [DS:SI]
 ↓
SI++
 ↓
AL == 0 ?
 ├── 是 → hang
 │
 └── 否
      ↓
    AH = 0x0E
      ↓
    INT 10h
      ↓
    BIOS 显示字符
      ↓
    回到 print
```

当前测试字符串：

```
A
```

因此 QEMU 最终能够看到：

```
A
```

---

# 28. 下一阶段学习路线

当前阶段暂时告一段落。

以后继续 LittleOS 时，可以按照下面路线继续。

---

## 阶段 1：BIOS 键盘输入

让 LittleOS 不再只是：

```
显示固定的 A
```

而是：

```
等待键盘
 ↓
按下一个键
 ↓
BIOS 返回按键
 ↓
AL 得到字符
 ↓
显示字符
```

例如最终做到：

```
LittleOS>

hello
```

---

## 阶段 2：做一个简单 Shell

例如：

```
LittleOS> help
LittleOS> clear
LittleOS> echo hello
LittleOS> about
```

这时候会开始真正产生“操作系统”的感觉。

---

## 阶段 3：Bootloader

把：

```
Boot Sector
```

逐渐变成：

```
Bootloader
```

学习：

- 磁盘读取
    
- 多扇区
    
- 内存加载
    
- 从 Bootloader 加载 Kernel
    

---

## 阶段 4：进入保护模式

学习：

```
Real Mode
     ↓
Protected Mode
```

涉及：

- GDT
    
- Segment
    
- 32 位寄存器
    
- CR0
    
- A20
    
- 保护模式启动
    

---

## 阶段 5：C 语言 Kernel

这是整个项目非常重要的一步：

```
Bootloader
    ↓
加载 kernel
    ↓
汇编启动代码
    ↓
C Kernel
```

开始真正学习：

```
void kernel_main(void)
{
}
```

以及：

```
C
 ↓
编译器
 ↓
汇编
 ↓
机器码
 ↓
链接
 ↓
Kernel
```

---

## 阶段 6：中断

学习：

```
Interrupt
```

包括：

- IDT
    
- ISR
    
- PIC
    
- 键盘中断
    
- 时钟中断
    

---

## 阶段 7：内存管理

学习：

- 栈
    
- 堆
    
- 页
    
- 分页
    
- Page Table
    
- 虚拟内存
    

---

## 阶段 8：进程和任务

进一步理解：

```
程序
 ↓
进程
 ↓
上下文切换
 ↓
调度
```

---

# 29. 复习题

以后重新开始学习之前，可以先尝试回答这些问题。

## 基础

### 1

CPU 真正执行的是：

```
A. 汇编语言
B. C 语言
C. 机器码
D. Python
```

答案：

```
C
```

---

### 2

```
mov al, 0x41
```

为什么能够变成：

```
B0 41
```

---

### 3

```
55 AA
```

为什么要放在 Boot Sector 的最后两个字节？

---

### 4

为什么 Boot Sector 必须是：

```
512 bytes
```

---

### 5

```
ORG 0x7C00
```

是不是 CPU 指令？

为什么？

---

### 6

```
db 'A', 0
```

CPU 会不会执行 `DB`？

---

## 寄存器

### 7

如果：

```
mov al, 0x41
```

执行以后：

```
AL = ?
```

---

### 8

如果：

```
mov ax, 0x1234
```

那么：

```
AH = ?
AL = ?
```

---

## FLAGS

### 9

执行：

```
cmp al, 0x41
```

CPU 实际进行的核心计算是什么？

---

### 10

如果：

```
AL = 0x41
```

那么：

```
cmp al, 0x41
```

以后：

```
ZF = ?
```

---

### 11

为什么：

```
je label
```

和：

```
jz label
```

机器码都是：

```
74 xx
```

---

## 跳转

### 12

为什么：

```
jmp label
```

不能简单理解成：

```
IP = label
```

而短跳转机器码通常包含一个：

```
相对偏移
```

---

### 13

计算：

```
当前位置：7C04
JMP 长度：2
目标：7C08
```

那么：

```
offset = ?
```

---

## Mini Assembler

### 14

为什么 Mini Assembler 需要“两遍汇编”？

---

### 15

第一遍主要做什么？

---

### 16

第二遍主要做什么？

---

### 17

为什么标签本身通常不会产生机器码？

---

# 30. 学习原则

LittleOS 后续学习继续遵循下面几个原则。

---

## 原则一：不背机器码

不要试图死记：

```
B0
B4
CD
F4
EB
74
75
```

真正要理解的是：

```
指令
 ↓
编码
 ↓
CPU行为
```

机器码自然会越来越熟。

---

## 原则二：每次只增加一点能力

例如：

```
MOV
 ↓
JMP
 ↓
LODSB
 ↓
TEST
 ↓
JZ
 ↓
CMP
 ↓
JE/JNE
```

一步一步增加。

---

## 原则三：所有东西都尽量验证

学习一个指令：

```
cmp al, 'A'
```

不要只相信解释。

可以：

```
nasm
```

生成机器码，然后：

```
ndisasm
```

反汇编，再通过：

```
QEMU
GDB
```

观察实际行为。

---

## 原则四：自己写 Mini Assembler

Mini Assembler 并不是为了替代 NASM。

真正目的：

> **通过自己实现一个极简汇编器，理解真正汇编器内部发生了什么。**

目前已经亲自实现了：

```
源代码解析
 ↓
识别指令
 ↓
识别操作数
 ↓
计算指令长度
 ↓
记录标签
 ↓
计算地址
 ↓
计算相对跳转
 ↓
生成机器码
 ↓
生成 512 字节 Boot Sector
 ↓
添加 55 AA
```

这是非常宝贵的底层经验。

---

# LittleOS 当前里程碑

截至目前：

```
[✓] Arch Linux 开发环境
[✓] NASM
[✓] QEMU
[✓] GDB 基础使用
[✓] 第一个 Boot Sector
[✓] 512 字节
[✓] 55 AA
[✓] BIOS 启动
[✓] QEMU 启动
[✓] 屏幕输出 Hello
[✓] 汇编 → 机器码
[✓] 机器码 → 反汇编
[✓] 寄存器基础
[✓] EIP / IP
[✓] ORG
[✓] BITS
[✓] DB
[✓] DW
[✓] 标签
[✓] 地址计算
[✓] 两遍汇编
[✓] JMP
[✓] 相对跳转
[✓] 自己编写 Mini Assembler
[✓] Mini Assembler 生成机器码
[✓] Mini Assembler 生成 Boot Sector
[✓] LODSB
[✓] DS:SI
[✓] TEST AL, AL
[✓] ZF
[✓] JZ
[✓] CMP
[✓] JE
[✓] JNE
[✓] LittleOS 成功显示 A
```

---

# 当前停止点

**这里就是本阶段的正式停止位置。**

最后已经理解：

```
CMP
 ↓
ZF
 ↓
JE / JNE
 ↓
条件改变程序执行路线
```

以及：

```
LittleOS
 ↓
Boot Sector
 ↓
CPU
 ↓
寄存器
 ↓
内存
 ↓
机器码
 ↓
FLAGS
 ↓
条件跳转
```

下一次继续 LittleOS 时，可以直接从：

> **BIOS 键盘输入：让 LittleOS 从“只能显示固定字符”进化到“能够读取用户输入”。**

开始。

---

# 一句话总结整个阶段

我们最开始只是让 QEMU 显示：

```
Hello from LittleOS!
```

现在已经能够自己解释：

```
汇编源代码
 ↓
汇编器
 ↓
机器码
 ↓
Boot Sector
 ↓
BIOS
 ↓
0x7C00
 ↓
CPU Fetch
 ↓
Decode
 ↓
Execute
 ↓
寄存器 / 内存 / FLAGS
 ↓
JMP / JZ / JE / JNE
 ↓
改变程序控制流
```

并且已经亲手写出了一个简化版的：

```
mini_asm.py
```

这就是 LittleOS 学习工程真正的第一阶段成果。