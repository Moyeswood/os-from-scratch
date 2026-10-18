---
tags:
  - OSDev
  - Computer_Science
  - Assembly
  - Linker
  - Memory
date: 2026-10-06
status:  完成
---

# 🧠 OSDev 底层原理深度剖析：指针、汇编、内存与链接

> [!abstract] 本文档包含
> - C语言指针、汇编与物理内存的三层关系
> - 汇编语言中“变量”的本质
> - 汇编器处理标签的“两遍扫描”魔术
> - 链接器修正地址的“三步走”战略
> - 重定位表（Relocation Table）的彻底解剖

---

## 一、C语言指针、汇编语言与物理内存的关系

### 💡 一句话核心结论

> **C 语言的指针，是逻辑上的“门牌号”；汇编语言，是 CPU 按照门牌号去“找门”的动作；而物理内存，是门后真实的“砖瓦和电路”。**

### 1. 第一层：C 指针在汇编里的“真面目”

在 C 语言里，我们写指针觉得很高级：
```c
char* vidptr = (char*)0xb8000; // 这是一个指针
*vidptr = 'A';                 // 解引用指针
```

但在汇编语言里，**根本没有“指针”这个数据类型**，只有**寄存器**和**内存访问指令（中括号 `[]`）**。

如果你用 `gcc -S` 把上面那两行 C 代码翻译成汇编，它会变成这样：

```nasm
; 对应 C 代码：char* vidptr = (char*)0xb8000;
mov eax, 0xb8000      ; 【汇编视角】：把常数 0xb8000 装进 eax 寄存器。
                      ; 【C语言视角】：把地址 0xb8000 赋值给指针变量 vidptr。
                      ; （此时，eax 寄存器就是那个指针！）

; 对应 C 代码：*vidptr = 'A';
mov byte [eax], 65    ; 【汇编视角】：把数字 65（'A'的ASCII码），
                      ; 写入到 eax 寄存器里保存的那个地址（0xb8000）所指向的内存中。
                      ; 【C语言视角】：对指针 vidptr 进行解引用（*）并赋值。
```

**🔑 顿悟时刻：**
C 语言里的 `*p`（解引用），在汇编里就是**中括号 `[]`**。
C 语言里的 `&a`（取地址），在汇编里就是**把变量所在的内存偏移量装进寄存器**。

### 2. 第二层：汇编地址如何变成“物理内存”？

当 CPU 执行 `mov byte [eax], 65` 时，物理层面到底发生了什么？

1. **CPU 发出电信号**：CPU 芯片边缘有几百根金色的引脚。其中有一组引脚叫**地址总线 (Address Bus)**（在 32 位系统里有 32 根）。
2. **地址解码**：CPU 把 `eax` 里的值 `0xb8000` 转换成高低电平，通过这 32 根线发送到主板。
3. **硬件路由**：主板上的**内存控制器 (Memory Controller)** 收到这个地址。它查了一下硬件地图，发现 `0xb8000` 不属于 RAM（内存条），而是属于显卡的 MMIO（内存映射 I/O）区域。
4. **物理动作**：内存控制器把电信号转发给显卡。显卡收到后，把像素点亮。

**如果是访问真正的内存条呢？**
假设 `eax` 里是 `0x100000`（位图所在的地址）：
内存控制器发现这是 RAM 区域，就会激活内存条上**第 1MB + 0 字节处的那颗微小的物理电容**，改变它的充放电状态（0 变 1，或 1 变 0）。

**🔑 顿悟时刻：**
汇编里的地址，就是 CPU 引脚上输出的**电平信号组合**。物理内存，就是能响应这些电平信号的**物理电路**。

### 3. 第三层：用 `memory.c` 走一遍全流程

**C 语言代码：**
```c
// 假设 bitmap 指针的值是 0x100000，byte_idx 是 5
bitmap[byte_idx] &= ~(1 << bit_idx);
```

**生成的汇编代码（带详细注释）：**
```nasm
mov eax, 0x100000     ; 1. 把 bitmap 的基址装进 eax
mov ecx, 5            ; 2. 把 byte_idx (5) 装进 ecx
mov dl, [eax + ecx]   ; 3. 【关键！】把 0x100005 地址里的 1 个字节，读到 dl 寄存器
                      ;    （这就是 C 语言里的 bitmap[5] 取值）
and dl, 0xFB          ; 4. 在寄存器 dl 里做位运算（把第 2 位清零）
mov [eax + ecx], dl   ; 5. 【关键！】把 dl 里的新值，写回 0x100005 地址
                      ;    （这就是 C 语言里的 bitmap[5] = ... 赋值）
```

**物理内存里发生了什么？**
- 执行第 3 行时：CPU 发出 `0x100005` 地址，内存条把那个电容的状态读出来，通过**数据总线 (Data Bus)** 送回 CPU 的 `dl` 寄存器。
- 执行第 5 行时：CPU 再次发出 `0x100005` 地址，并通过数据总线把新数据写回那个电容。

### 4. 裸机开发 (OSDev) 中的“残酷真相”

在 Linux 下写 C 语言，操作的是**虚拟内存**。指针 -> 汇编地址 -> **MMU 页表翻译** -> 物理内存。

**但在你的 `myQwen-os` 里（还没有开启分页机制 Paging）：**
> **C 指针 = 汇编地址 = 物理地址！**

这意味着你在 C 语言里写下的每一个指针，都在**真真切切地、毫无保护地**操作着物理内存条！写错一个字节，就是万劫不复（Triple Fault）。

---

## 二、汇编语言中没有“变量”，只有“地址+字节”

> **在汇编语言（以及更底层的机器码）的世界里，绝对没有“变量”这个概念。CPU 的眼里只有三样东西：寄存器、地址、字节。**

### 1. 真相一：变量只是“地址 + 类型”的伪装

在 C 语言里：
```c
int count = 10;
char flag = 'A';
```

**但在汇编眼里：**
```nasm
; 假设 count 被分配在内存地址 0x10000
; 假设 flag 被分配在内存地址 0x10004

mov dword [0x10000], 10   ; 把数字 10 塞进 0x10000 开始的 4 个字节里
mov byte  [0x10004], 65   ; 把数字 65 塞进 0x10004 开始的 1 个字节里
```

**🔑 顿悟时刻：**
C 语言所谓的“变量”，在底层只是 **“一个特定的内存地址”** 加上 **“编译器记住的要读几个字节（类型）”**。
- `int` = 读 4 个字节 (`dword`)
- `char` = 读 1 个字节 (`byte`)

### 2. 真相二：汇编里的“变量名”只是编译时的“占位符”

汇编代码里的名字（如 `my_var db 10`）只是一个**标签 (Label)**。它的作用仅仅是告诉 NASM 汇编器：“请帮我算一下，这个数据在内存里的具体地址是多少。”

当你汇编并链接后，生成的机器码里**绝对没有 `my_var` 这四个字母**，只有冷冰冰的地址：
```nasm
; 汇编器在后台把 my_var 替换成了它计算出的真实地址，比如 0x4000
mov al, [0x4000] 
```

### 3. 终极对比图：C 变量 vs 汇编地址

| 概念 | C 语言视角 (人类思维) | 汇编/机器码视角 (CPU 思维) |
| :--- | :--- | :--- |
| **变量声明** | `int a = 5;` | 编译器在栈或数据段预留 4 字节空间。 |
| **变量名** | `a` | **不存在**。编译后变成具体的偏移量（如 `[ebp-4]`）或绝对地址（如 `[0x10000]`）。 |
| **数据类型** | `int` (决定能存多大) | **不存在**。变成指令后缀（如 `mov dword` 表示操作 4 字节，`mov byte` 表示 1 字节）。 |
| **赋值操作** | `a = 10;` | `mov dword [具体地址], 10` (把数据搬到地址指向的内存)。 |
| **指针** | `int *p = &a;` | `mov eax, 具体地址` (把地址本身当成普通数据存进寄存器)。 |

---

## 三、汇编器如何理解标签（两遍扫描魔术）

> **汇编器根本没有“理解”代码，它只是一个“带着计算器的无情文本替换机器”。**

假设你的 `boot.asm` 里有这样一段代码：
```nasm
[BITS 16]
[ORG 0x7C00]

start:
    mov ax, 0x02
    int 0x13
    jc disk_error      ; <--- 这里用到了标签
    jmp start          ; <--- 这里也用到了标签

disk_error:            ; <--- 这里定义了标签
    mov al, 'E'
    int 0x10
    jmp $
```

当你运行 `nasm -f bin boot.asm -o boot.bin` 时，NASM 在后台做了两件事：

### 1. 第一遍扫描（Pass 1）：只算地址，不生成机器码

汇编器从头到尾把代码读一遍，**它的唯一任务是计算每一行代码在内存中的“偏移量（Offset）”，并建立一个“字典”（符号表 Symbol Table）。**

它心里是这样计算的：
1. `start:` 在 `0x7C00`。
2. `mov ax, 0x02` 占 3 个字节。所以下一行在 `0x7C03`。
3. `int 0x13` 占 2 个字节。所以下一行在 `0x7C05`。
4. `jc disk_error` 占 2 个字节。所以下一行在 `0x7C07`。
5. `jmp start` 占 2 个字节。所以下一行在 `0x7C09`。
6. **发现标签 `disk_error:`！** 汇编器在字典里记下一笔：`disk_error = 0x7C09`。

**第一遍结束后，汇编器手里有了一张完美的“地址字典”：**
```text
Symbol Table (符号表):
  start      -> 0x7C00
  disk_error -> 0x7C09
```

### 2. 第二遍扫描（Pass 2）：生成机器码，查字典填坑

汇编器再次从头读代码，这次它开始**真正生成 0 和 1 组成的机器码**。

1. 读到 `mov ax, 0x02` -> 翻译成 `B8 02 00`。
2. 读到 `int 0x13` -> 翻译成 `CD 13`。
3. 读到 `jc disk_error`：
   - 汇编器知道 `jc` (Jump if Carry) 的机器码是 `72`。
   - 但是后面跟的偏移量是多少呢？它去查第一遍生成的“字典”！
   - 字典说 `disk_error` 在 `0x7C09`。
   - 当前指令在 `0x7C05`，加上指令长度 2 字节，下一条指令在 `0x7C07`。
   - 偏移量 = 目标地址 - 下一条指令地址 = `0x7C09 - 0x7C07 = 2`。
   - 于是，它生成机器码：`72 02`。
4. 读到 `disk_error:`：
   - 汇编器心里一笑：“哼，这只是一个标签，不占任何字节，不生成任何机器码！”直接跳过，继续翻译后面的 `mov al, 'E'`。

### 3. 标签的两种“物理形态”

1. **作为“跳转目标”（相对偏移）**：当你写 `jc disk_error` 时，标签被转换成了**相对偏移量**。本质是一个加减法的算式（目标地址 - 当前地址）。
2. **作为“数据地址”（绝对地址）**：当你写 `mov ax, my_var` 时，标签被转换成了**绝对地址**。本质是一个真实的内存门牌号。

---

## 四、链接器如何修正地址（三步走战略）

> **如果说汇编器/编译器是“近视眼”（只能看到单个文件），那么链接器（Linker）就是拥有“上帝视角”的“总包工头”。**

### 1. 为什么需要“修正”？（编译器留下的“坑”）

假设你的 `kernel.c` 调用了 `serial.c` 里的函数：
```c
// kernel.c
extern void serial_puts(const char* str); // 告诉编译器：这函数在别处

void kernel_main() {
    serial_puts("Hello OS!"); // 调用外部函数
}
```

当你用 `gcc -c kernel.c` 编译时，编译器**根本不知道 `serial_puts` 最终会被放在内存的哪个地址**。

那编译器怎么办？它会**先挖个坑（填 0）**，并留下一张“欠条”（重定位表）：
1. 生成机器码：`E8 00 00 00 00` （`call 0x00000000`，先假装地址是 0）。
2. 在生成的 `kernel.o` 文件里，附带一张**重定位表（Relocation Table）**，上面写着：
   > “喂，链接器！在代码偏移量 `0x15` 的地方有个坑（0），请帮我把它替换成 `serial_puts` 的真实地址！”

### 2. 链接器的“三步走”战略

当你运行 `ld -T linker.ld kernel.o serial.o -o kernel.elf` 时，链接器开始大展身手：

#### 第一步：布局（Layout）：根据“总图纸”排座位
链接器首先读取你写的 **`linker.ld`（链接脚本）**。
```ld
SECTIONS {
    . = 0x10000;       // 规定：整个内核从物理地址 0x10000 开始！
    .text : { 
        *(.text.start) // 先放 entry.o
        *(.text)       // 再放 kernel.o, serial.o 的代码
    }
}
```
链接器把 `kernel.o` 和 `serial.o` 的代码段（.text）像排队一样拼在一起，并**计算出每个函数的最终绝对物理地址**：
- `kernel_main` 起始地址：`0x10000`
- `serial_puts` 起始地址：`0x10050` （假设它排在后面）

#### 第二步：合并符号表（Symbol Resolution）：合并“字典”
链接器把 `kernel.o` 和 `serial.o` 里的“符号表（字典）”合并成一本大字典：
```text
全局符号表 (Global Symbol Table):
  kernel_main -> 0x10000
  serial_puts -> 0x10050
```

#### 第三步：地址修正（Relocation）：填坑！
这是最核心的一步！链接器拿着 `kernel.o` 留下的“重定位表”，开始填坑：

- **找到坑**：在 `kernel_main` 的代码里，偏移 `0x15` 处有个 `00 00 00 00`。
- **查字典**：这个坑对应的是 `serial_puts`。查大字典，`serial_puts` 的地址是 `0x10050`。
- **计算偏移并修改**：x86 的 `call` 指令通常是相对跳转。链接器算出相对偏移，把机器码 `00 00 00 00` 修改为正确的偏移量（比如 `36 00 00 00`）。

**填坑完成后，机器码变成了：**
`E8 36 00 00 00` （`call 0x10050`）

---

## 五、什么是重定位表（Relocation Table）

> **重定位表，就是编译器（近视眼）留给链接器（总包工头）的一份“待办事项清单”，上面精确记录了：“我在代码的哪个位置留了空白，请你最后帮我填上哪个函数的真实地址。”**

### 1. 重定位表里到底写了什么？

这张表通常藏在 `.o`（目标文件）的特定段里（比如 `.rel.text`）。它主要由三个核心字段组成：

| 字段 | 含义 | 通俗解释 |
| :--- | :--- | :--- |
| **Offset (偏移量)** | 需要修改的地址在代码中的位置 | “坑”在图纸的哪一行、哪一列？ |
| **Info / Symbol (符号)** | 需要填入哪个函数/变量的地址 | 这个坑要填上谁的名字？（比如 `serial_puts`） |
| **Type (类型)** | 怎么修改（绝对地址还是相对偏移） | 填坑的“工艺”是什么？（x86 的 `call` 通常是相对跳转） |

### 2. 实战解剖：你的 `kernel.o` 里的重定位表

如果你在你的 Arch Linux 终端里运行 `readelf -r kernel/kernel.o`，你会看到类似这样的输出（这就是重定位表的真面目）：

```text
Relocation section '.rel.text' at offset 0x... contains 1 entries:
 Offset     Info    Type            Sym.Value  Sym. Name
00000015  00000501 R_386_32          00000000   serial_puts
```

**让我们逐列“破译”这张表：**

1. **`Offset: 00000015`**
   - **含义**：在 `kernel.o` 的机器码中，从第 `0x15` 个字节开始，有一个 4 字节的“坑”（目前是 `00 00 00 00`）。
   - **通俗解释**：“总包工头，请看图纸第 21 行（0x15），那里有个空白插座！”

2. **`Sym. Name: serial_puts`**
   - **含义**：这个坑需要填入 `serial_puts` 这个符号的最终地址。
   - **通俗解释**：“这个插座是留给 `serial_puts` 函数的！”

3. **`Type: R_386_32`**
   - **含义**：这是 32 位 x86 架构的**绝对地址重定位**（如果是 `R_386_PC32` 则是相对地址重定位，用于 `call` 指令）。
   - **通俗解释**：“请直接把 `serial_puts` 的 32 位绝对物理地址填进去！”

### 3. 为什么理解这个，能帮你解决 99% 的链接报错？

#### 1. `undefined reference to 'pic_remap'`
- **真相**：链接器在读取 `kernel.o` 的重定位表时，发现表里写着：“请在偏移 `0x31` 处填入 `pic_remap` 的地址”。但是，链接器翻遍了所有 `.o` 文件的符号表，**根本找不到 `pic_remap` 这个符号**！
- **解决**：你忘了把 `pic.c` 编译成 `pic.o` 并传给链接器（就像你之前遇到的 Makefile 漏掉 `pic.o` 的 Bug）。

#### 2. `multiple definition of 'kernel_main'`
- **真相**：链接器发现两个不同的 `.o` 文件里，都定义了 `kernel_main` 这个符号。重定位表里如果引用了它，链接器就不知道该填哪个地址了。
- **解决**：检查是不是不小心把同一个 `.c` 文件编译了两次，或者在头文件里写了函数实现而不是声明。

#### 3. 为什么 `static` 函数不会出现在重定位表里？
- **真相**：如果你在 `kernel.c` 里写 `static void helper() {}`，编译器知道这个函数**只在这个文件内部使用**。它会在编译 `kernel.c` 时直接算出 `helper` 的相对地址并填好，**根本不会把它放进重定位表里交给链接器**。这就是 `static` 能减少链接冲突的原因。

---

##  终极总结：三者的完美分工

- **汇编器/编译器（Assembler/Compiler）**：负责把单个人类语言翻译成机器码，遇到跨文件引用就**挖坑留欠条（重定位表）**。
- **链接脚本（Linker Script）**：负责提供**建筑总图纸**，规定物理内存的绝对布局。
- **链接器（Linker）**：负责**排队、算地址、查字典、填坑**，最终生成 CPU 能直接加载的完整 ELF 文件。

汇编语言里面汇编器是怎么理解标签和真正的汇编代码的

这个问题问得很好，涉及汇编器（assembler）最核心的工作机制。我分几个层面来讲。

## 一、汇编器的基本工作流程

汇编器把汇编源文件翻译成机器码，通常要经过**两遍扫描（two-pass）**，原因正是标签的存在。

### 第一遍：建立符号表（symbol table）

汇编器逐行读取源码，但**不生成最终机器码**（或只生成部分），主要做两件事：

1. **确定每条指令的长度和地址**
    
    - 因为不同指令编码长度不同（x86 从 1 字节到 15 字节不等），汇编器需要先"走一遍"确定每条指令在内存中的位置。
        
    - 维护一个**位置计数器（Location Counter, LC）**，也叫当前地址指针。
        
2. **记录所有标签的地址**
    
    - 遇到 `label:` 时，把 `label` 和当前 LC 的值存入符号表。
        
    - 例如：
        
        asm
        
        start:  mov eax, 1    ; 假设这条指令 5 字节，LC=0x1000
                jmp start     ; 此时 start 已知 = 0x1000
        loop:   ...
        
    - 第一遍结束后，符号表里就有 `start -> 0x1000`、`loop -> 0x1005` 等。
        

### 第二遍：生成机器码

再次扫描源码，这次：

- 遇到指令就真正编码成机器码。
    
- 遇到标签引用（如 `jmp start`），去符号表查它的地址，填进指令的操作数里。
    
- 处理 `.equ`、重定位等。
    

**这就是为什么前向引用（forward reference）也能工作**——第一遍已经把标签地址记下来了。

---

## 二、汇编器如何"理解"标签

关键在于：**标签对汇编器来说不是"变量"，而是一个符号（symbol），代表某个地址或常量。**

### 1. 标签的分类

|类型|例子|含义|
|---|---|---|
|代码标签|`start:`|某条指令的地址|
|数据标签|`msg: db "hi"`|某个数据字节的地址|
|常量标签|`MAX equ 100`|一个纯数值，不占地址|
|段/节标签|`section .text`|段名，不是普通标签|

### 2. 符号表的结构

汇编器内部维护一张表，每项大致是：

text

名字        类型      值/地址     段     绑定(global/local)
start       code      0x1000     .text   local
msg         data      0x2000     .data   global
MAX         absolute  100        -       -

- **值**：可能是地址（可重定位），也可能是绝对常量。
    
- **段信息**：链接时需要知道标签属于哪个段，才能算最终地址。
    
- **绑定**：`global` 的标签要导出给其他目标文件用；`local` 只在文件内可见。
    

### 3. 标签的解析时机

- **同一文件内、同一段内**的引用：第二遍就能直接算出相对/绝对地址（如果位置无关则算偏移）。
    
- **跨文件或跨段**的引用：汇编器**填不了最终值**，只能留一个**重定位项（relocation entry）**，交给链接器处理。
    

例如：

asm

call printf    ; printf 在别的文件，汇编器不知道地址

汇编器生成一条 `call` 指令，操作数位置填 0，并记一条重定位：

text

偏移: 0x01  类型: R_X86_64_PLT32  符号: printf  加数: -4

链接器之后再去填真正的地址。

---

## 三、标签"理解"的几个细节

### 1. 局部标签 vs 全局标签

- 全局标签（`.global`）：进入目标文件的符号表，可被链接。
    
- 局部标签（默认或 `.local`）：只在当前文件有效，链接器看不到。
    

### 2. 数字局部标签（GNU as 风格）

asm

1:  jmp 1b    ; 向后找最近的 1
    jmp 1f    ; 向前找最近的 1

汇编器维护一个"最近定义的 1"的栈，`1b`=backward，`1f`=forward。这类标签不进普通符号表。

### 3. 标签的值取决于段

同一个名字的地址在不同段里意义不同：

asm

section .text
foo: ...
section .data
bar: ...

`foo` 和 `bar` 的"值"都是段内偏移，最终地址 = 段基址 + 偏移，由链接器定。

### 4. 位置无关代码（PIC）

现代汇编器对标签可能生成 **GOT/PLT 引用**，而不是绝对地址：

asm

mov rax, [rip + msg]     ; RIP 相对寻址
lea rax, [rip + msg]

这时标签被理解成"相对于当前指令的偏移"，汇编器能直接算出来，不需要重定位（同段内）。

---

## 四、一个完整的小例子

asm

section .data
    msg db "Hello", 0
section .text
    global _start
_start:
    mov rax, 1          ; sys_write
    mov rdi, 1
    lea rsi, [rel msg]  ; 引用 msg
    mov rdx, 5
    syscall
    mov rax, 60
    xor rdi, rdi
    syscall

汇编器处理过程：

1. **第一遍**
    
    - `.data` 段：`msg` 在偏移 0，长度 6。
        
    - `.text` 段：`_start` 在偏移 0；逐条累加指令长度，得到每条指令偏移。
        
    - 符号表：`msg -> (.data, 0)`，`_start -> (.text, 0, global)`。
        
2. **第二遍**
    
    - `lea rsi, [rel msg]`：算出 `msg` 相对当前 RIP 的偏移，直接编码。
        
    - `_start` 是 global，导出到符号表。
        
    - 其他指令直接编码。
        
3. **输出**
    
    - 一个 `.o` 目标文件，含 `.text`、`.data` 段、符号表、重定位表。
        
4. **链接器**
    
    - 把各段合并，确定最终基址，修正所有重定位。
        

---

## 五、一句话总结

> **汇编器把标签当作"符号"而不是"变量"：第一遍扫描确定每个标签的地址/值并记入符号表，第二遍扫描时用符号表里的值去填充指令中的引用；能算的当场算，算不了的（跨文件/跨段）留重定位项给链接器。**

如果你想深入某一点——比如重定位表的具体格式、x86 指令编码怎么算长度、或者链接器如何修正地址——我可以继续展开。

x86 指令编码怎么算长度，链接器如何修正地址

Found 20 web pages

这两个问题恰好是汇编过程中“编码”与“链接”的核心。我分别来展开。

### 📏 x86 指令长度如何计算

x86 指令是**变长**的，从 1 字节到 15 字节不等[](https://patentimages.storage.googleapis.com/a3/57/fe/6451a9de5d1f82/US6081884.pdf#3#1)。汇编器计算长度的本质，就是**按固定顺序逐段累加各字段的长度**。

**指令的通用结构（按顺序排列）**[](https://www.zhihu.com/question/396397874/answer/1242663983?utm_medium=openapi_platform&utm_source=e10c3ad29e50)[](https://patentimages.storage.googleapis.com/a3/57/fe/6451a9de5d1f82/US6081884.pdf#3#1)：

|字段|长度|说明|
|---|---|---|
|**前缀 (Prefixes)**|0~5 字节|可选。如 `0x66` 改操作数大小，`0x67` 改地址大小，`REX` 开启 64 位寄存器等|
|**Opcode**|1~3 字节|必需。指定指令功能，如 `0x55` 就是 `push ebp`|
|**ModR/M**|0~1 字节|可选。指定操作数是寄存器还是内存，以及内存寻址模式|
|**SIB**|0~1 字节|可选。配合 ModR/M 实现 `base + index*scale + disp` 寻址|
|**Displacement (偏移)**|0~4 字节|可选。地址计算中的常量|
|**Immediate (立即数)**|0~4 字节|可选。指令中的常量操作数|

**计算逻辑就是累加**：汇编器读取当前指令，识别哪些字段存在，然后把它们的长度加起来。比如 `mov eax, 1`：

1. 无前缀（0 字节）
    
2. Opcode 是 `B8`（1 字节）
    
3. 无 ModR/M（因为是寄存器直接寻址，操作数在 Opcode 里）
    
4. 无 SIB
    
5. 无偏移
    
6. 立即数 `1` 占 4 字节（32 位）
    
7. **总长 = 0 + 1 + 0 + 0 + 0 + 4 = 5 字节**
    

而 `xorq %rax, %rax` 比 32 位版本多一个 **REX 前缀**（1 字节），所以长度会不一样[](https://www.zhihu.com/question/53278372)。

汇编器第一遍扫描时就是靠这个累加逻辑，确定每一条指令的起始地址，从而给标签赋值的。

---

### 🔗 链接器如何修正地址

汇编器输出目标文件时，每个段都**假装从地址 0 开始**。段内标签的值实际上是“段内偏移”[](https://www.ti.com/document-viewer/lit/html/SLAU131Y/GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD.html#1)。但程序真正运行时，段会被加载到某个实际基址上，这个基址汇编器不知道。

**链接器的修正逻辑就是：最终地址 = 段基址 + 段内偏移。**

具体步骤：

1. **分配地址**：链接器把所有目标文件的相同段合并，然后根据内存布局（如 `MEMORY` 指令）给每个合并后的段分配一个**运行时基址**[](https://www.ti.com/jp/lit/ug/spru186s/spru186s.pdf?ts=1743743333886&ref_url=https%253A%252F%252Fwww.ti.com%252Fjp%252Flitv%252Fpdf%252Fspru186s#12#4)[](https://www.ti.com/document-viewer/lit/html/SLAU131Y/GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD.html#1)。
    
2. **更新符号值**：原来标签的值是“段内偏移”，现在把它更新为“基址 + 偏移”，得到真正的运行时地址。例如 `.text` 基址被定为 `0x7200`，原来偏移为 `16` 的 `Y`，新值就变成 `0x7210`[](https://www.ti.com/jp/lit/ug/spru186s/spru186s.pdf?ts=1743743333886&ref_url=https%253A%252F%252Fwww.ti.com%252Fjp%252Flitv%252Fpdf%252Fspru186s#12#4)[](https://www.ti.com/document-viewer/lit/html/SPRUI03F/GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD.html#GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD#1)。
    
3. **修正引用**：链接器遍历**重定位表**（汇编器在遇到可重定位符号引用时留的“便签”），找到代码中所有引用该符号的位置，用新计算出的值去**打补丁**[](https://www.ti.com/jp/lit/ug/spru186s/spru186s.pdf?ts=1743743333886&ref_url=https%253A%252F%252Fwww.ti.com%252Fjp%252Flitv%252Fpdf%252Fspru186s#12#4)。
    

重定位有不同的类型，计算方式也不同[](https://zhuanlan.zhihu.com/p/42495476#1)：

- **R_X86_64_32（绝对地址）**：公式是 `S + A`。`S` 是符号最终地址，`A` 是引用处原有的值（通常是 0 或一个偏移）。链接器直接把 `S + A` 写入代码。
    
- **R_X86_64_PC32（相对地址）**：公式是 `S + A - P`。`P` 是引用处所在的地址（比如 call 指令的下一条）。用于函数调用，算出来的是“目标相对于当前位置的偏移量”。
    

**一个直观的例子**：汇编器看到 `call printf`，但不知道 `printf` 在哪，于是生成 `E8 00 00 00 00`（call 后面填 0），并记一条重定位项：“地址 `0x12` 处，类型 `PC32`，符号 `printf`”。链接器最终算出 `printf` 和当前位置的距离是 `-0x20`，就把 `00 00 00 00` 改成 `E0 FF FF FF`[](https://www.ti.com/jp/lit/ug/spru186s/spru186s.pdf?ts=1743743333886&ref_url=https%253A%252F%252Fwww.ti.com%252Fjp%252Flitv%252Fpdf%252Fspru186s#12#4)[](https://www.ti.com/document-viewer/lit/html/SPRUI03F/GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD.html#GUID-F1D8CB49-1F0F-4A83-BB5E-B9591401F3CD#1)。

对于**动态链接**，情况更复杂：`printf` 在共享库里，地址运行时才确定。链接器会生成 `PLT`（过程链接表）和 `GOT`（全局偏移表）的桩代码。`call` 实际跳转到 `PLT`，由 `PLT` 通过 `GOT` 在运行时解析真实地址[](https://zhuanlan.zhihu.com/p/134105591#1)。

重定位表的具体格式

Found 18 web pages

重定位表（Relocation Table）本质上是一个由“重定位项”构成的数组。每个重定位项是一个结构体，记录了“在哪个位置、对哪个符号、用哪种方式”进行地址修正。

### 📋 重定位项的结构

在 ELF 标准中，重定位项有两种结构：`Rel`（无显式加数）和 `Rela`（有显式加数）[](https://docs.oracle.com/cd/E19957-01/806-0641/6j9vuqujo/index.html#4)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)。x86-64 架构使用 `Rela` 结构，包含三个字段[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)[](https://manpages.ubuntu.com/manpages/focal/fr/man5/elf.5.html#4)：

|字段|类型 (64位)|含义|
|---|---|---|
|**`r_offset`**|`Elf64_Addr`|**修正位置**：指定需要被修改的存储单元的位置。在目标文件（.o）中，它是相对于所在段的偏移量；在可执行文件或共享库中，它是虚拟地址[](https://docs.oracle.com/cd/E19957-01/806-0641/6j9vuqujo/index.html#4)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)。|
|**`r_info`**|`Elf64_Xword`|**符号与类型**：这是一个复合字段，同时编码了“符号表索引”和“重定位类型”[](https://docs.oracle.com/cd/E19253-01/819-7050/6n918j8nn/index.html#5)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)。|
|**`r_addend`**|`Elf64_Sxword`|**加数**：一个常量，在计算修正值时参与运算。`Rela` 结构明确包含此字段，而 `Rel` 结构则将加数隐式地存放在需要被修改的位置中[](https://docs.oracle.com/cd/E19957-01/806-0641/6j9vuqujo/index.html#4)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)。|

### 🔍 字段的细节

**1. `r_info` 如何编码符号和类型**

`r_info` 是一个整数，通过位运算可以拆解出两个信息[](https://docs.oracle.com/cd/E19253-01/819-7050/6n918j8nn/index.html#5)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)：

- **符号表索引**：`ELF64_R_SYM(info) = info >> 32`。指向符号表中的一个条目，标识引用的是哪个符号（如函数名或变量名）。
    
- **重定位类型**：`ELF64_R_TYPE(info) = (Elf64_Word)info`。一个低 32 位的值，告诉链接器如何计算修正值。例如，`R_X86_64_PC32` 的值是 2[](https://kunit.googlesource.com/linux/+/fbff868db3a4cc6a89d51da9a6d49b26c29d04fb/include/asm-x86_64/elf.h)。
    

**2. `r_offset` 的语境差异**

同一个字段，在不同类型的 ELF 文件中含义不同[](https://docs.oracle.com/cd/E19957-01/806-0641/6j9vuqujo/index.html#4)[](https://docs.oracle.com/cd/E19253-01/819-7050/chapter6-54839/index.html#1)：

- **在可重定位文件（.o）中**：`r_offset` 是**节偏移**。它告诉链接器：“在我这个目标文件里，这个节（比如 `.text`）的第 X 字节处有个引用需要你修正。”
    
- **在可执行文件/共享库中**：`r_offset` 是**虚拟地址**。它告诉运行时链接器：“在内存的这个地址上，有个引用需要你修正。”
    

### 📌 x86-64 常见重定位类型

重定位类型决定了链接器如何计算最终的修正值。计算中会用到一些标准符号，如 `S`（符号最终地址）、`A`（加数）、`P`（修正位置的地址）[](https://docs.oracle.com/cd/E19957-01/806-0641/6j9vuqujo/index.html#4)[](https://docs.oracle.com/cd/E26502_01/html/E26507/chapter6-54839.html#chapter7-2#1)。

|类型|值|计算公式|典型用途|
|---|---|---|---|
|**`R_X86_64_64`**|1|`S + A`|绝对地址引用（64位）|
|**`R_X86_64_PC32`**|2|`S + A - P`|PC 相对寻址的 32 位偏移（如 `call`、`jmp`）[](https://android.googlesource.com/toolchain/lld/+/71696db27ba0f862f6722eccd7edc12bdee54ce3/lib/ReaderWriter/ELF/X86_64/X86_64RelocationHandler.cpp#1)[](https://kunit.googlesource.com/linux/+/fbff868db3a4cc6a89d51da9a6d49b26c29d04fb/include/asm-x86_64/elf.h)|
|**`R_X86_64_PLT32`**|4|`L + A - P`|调用外部函数，指向 PLT 桩|
|**`R_X86_64_GOTPCREL`**|9|`G + GOT + A - P`|通过 GOT 访问外部数据|

### 💡 一个直观的类比

你可以把重定位表想象成一张**“待修订单”**：

- **`r_offset`**：要修改的**页码和行号**。
    
- **`r_info`**：写着“把这里改成**哪个名字**（符号）的地址”以及“用**哪种公式**（类型）算”。
    
- **`r_addend`**：一个**附加的调整数**。
    

链接器的工作，就是拿着这张单子，找到符号的最终地址，按公式算好，再填回 `r_offset` 指定的位置。


# 理解计算机内存管理，是学习任何编程语言的"内功"。可以从这几个层次逐步理解：

## 1. 先建立物理直觉：内存是什么

内存就是一排编号的"小格子"（字节 byte），每个格子有唯一地址。CPU 本质上只做两件事：**从某个地址读数据**、**往某个地址写数据**。所有高级语言最终都会变成这两类操作。

打个比方：内存像一栋大楼，每个房间（字节）有房号（地址），房间里的住户是 0~255 的数字。

## 2. 编译型语言的视角（C/C++/Rust/Go）

这是理解底层的最佳入口，因为你必须亲手管理内存的"生死"：

- **栈（Stack）**：局部变量住这里。函数调用时自动分配，函数返回时自动回收。速度快，但空间有限（通常几 MB）。像"快捷酒店"，入住退房全自动。
    
- **堆（Heap）**：`malloc`/`new` 申请的空间。大、灵活，但你必须记住 `free`/`delete`——忘记就**内存泄漏**，提前释放就**悬空指针**（野指针）。像"长租公寓"，自己签合同自己退。
    
- **变量的本质**：变量名只是地址的"别名"。`int x = 42` 的真实含义是：在某地址写入了 4 字节数据。指针就是"存着别人房号的纸条"。
    

## 3. 操作系统视角

多个程序同时运行，谁都可以用内存吗？不行，OS 做了关键抽象：

- **虚拟内存**：每个进程看到的都是"从 0 开始的完整地址空间"，互相隔离、互不干扰。OS + CPU 的 MMU（内存管理单元）负责把虚拟地址翻译成物理地址。
    
- **分页（Paging）**：内存被切成固定大小的页（通常 4KB），按需映射。用不到的页可以换到硬盘上（这就是"内存不够时电脑变卡"的原因）。
    
- **内存保护**：进程 A 不能读写进程 B 的内存——这是安全性的根基，也是"段错误（Segmentation Fault）"的由来。
    

## 4. 垃圾回收语言视角（Java/Python/JS）

你只管 `new`，不用管释放。回收靠什么？

- **引用计数**（Python 主要方式）：每个对象记着"有多少人在用我"，归零就销毁。缺点是无法处理循环引用（A 引用 B，B 引用 A，互相拖累谁也别想死）。
    
- **可达性分析 / 标记-清除**（Java 的 GC）：定期从"根"（全局变量、栈上的引用）出发扫描，扫不到的整个回收。代价是会有停顿（STW）。
    
- 理解了手动管理的痛苦，才能理解 GC 为什么这样设计。
    

## 5. 推荐的学习路径

1. **先学 C**：写代码故意触发内存错误（越界访问、double free、use-after-free），用 `valgrind` 或 AddressSanitizer 观察报错——亲手犯错的记忆最深。
    
2. **画内存图**：学任何新概念时画格子图：栈帧怎么长、指针指向哪、对象在堆里怎么布局。
    
3. **观察真实工具**：Linux 下用 `top`/`pmap`/`/proc/<pid>/maps` 看进程真实的内存布局。
    
4. **逆向一个小程序**：用 `gdb` 看变量在内存中的真实字节（`x/16xb &变量名`），感受"一切皆是字节"。
    

## 一个核心心智模型

> 内存管理只有三个问题：**何时分配**（编译期/运行时）、**放在哪里**（栈/堆/静态区）、**何时回收**（手动/作用域结束/GC）。

每学一门新语言，都用这三个问题去套它，你会发现所有语言的内存模型都是这三问的不同答案。

想深入的话，我建议从 C 的栈帧和指针开始实操——你想让我给你出个练习题，还是展开讲某一部分（比如虚拟内存或 GC）？

学习汇编语言，怎么理解计算机底层是怎么管理内存的

Edit

Copy

Share

汇编低级内存管理基础深入概览教程

User asks: learning assembly, how to understand memory management at low level. This is a follow-up to previous question. Should be educational, no tools needed. Respond in Chinese. Maybe use a widget? Probably not necessary; text with diagrams is fine. Could offer an ASCII memory map. Keep focused on assembly perspective: flat memory model, segments (x86 real mode vs protected mode), registers, addressing modes, stack operations (push/pop, call/ret), memory layout of a process (text/data/bss/heap/stack), alignment, endianness, addressing mode examples.

Could use show_widget? Only if it helps. A text explanation with diagrams is fine. Let me write a good answer with concrete examples (x86-64 AT&T syntax).

汇编语言是观察内存管理"裸机真相"的最佳窗口——因为没有编译器帮你藏东西，每一条指令读写哪个地址，全都一目了然。建议按下面几条线索建立理解：

## 1. 内存就是地址空间，CPU 只认地址

在汇编里没有"变量"这个概念，只有**地址 + 字节**。你写的：

asm

```asm
mov  $42, -4(%rbp)     # 把 42 存到 rbp-4 这个位置
```

翻译成大白话就是："把数字 42 写进内存地址 rbp-4 开始的 4 个字节"。所谓"变量名"是高级语言的幻觉，汇编里一切内存操作都长这样：

asm

```asm
mov  (%rax), %rbx      # 把 rax 里存的地址所指向的内容读进 rbx —— 这就是"解引用指针"
```

学到这里你就会明白：**指针不是 C 特有的东西，而是硬件层面唯一存在的东西。**

## 2. 地址怎么算出来：寻址模式

x86 的寻址公式是本节的核心：

plain

```plain
有效地址 = 基址寄存器 + (变址寄存器 × 比例) + 偏移量
   [base + index*scale + disp]
```

对应语法：

asm

```asm
mov  8(%rax,%rcx,4), %edx   # 读 rax + rcx*4 + 8 处的 4 字节
```

这条公式解释了你学过的所有东西：

- **数组**：`arr[i]` → `arr 的基址 + i × 元素大小`，比例 1/2/4/8 正好是 8/16/32/64 位元素。
    
- **结构体**：固定偏移量，比如 `person->age` → `基址 + 8`。
    
- **局部变量**：`rbp`（栈基址）做基址，负偏移。
    

**练习**：写一个循环遍历 int 数组的汇编，亲手用这条公式算每个元素的地址，数组就再也不是黑盒了。

## 3. 栈是"有纪律的内存"，指令集亲自维护

这是汇编最有价值的视角——**栈不是靠语言实现的，而是 CPU 指令和寄存器直接支持的**：

- `%rsp`：栈指针，永远指向栈顶。`push` = `rsp -= 8; 写入`；`pop` = `读出; rsp += 8`。
    
- `%rbp`：帧指针，标记"本次函数调用的地盘"。
    
- `call` = `push 返回地址 + 跳转`；`ret` = `弹出返回地址 + 跳转`。
    

所以一次函数调用的完整内存账本是：

plain

```plain
高地址
... 上一个函数的栈帧
返回地址        <- call 指令压入
保存的旧 rbp    <- 函数序言 push %rbp
局部变量 A      <- sub $N, %rsp 留出空间
局部变量 B
低地址（rsp 指向这里）
```

用 `gdb` 单步走一遍函数调用，观察 `rsp`/`rbp` 和栈上内容的变化——你会真正理解"栈帧"这个词，而不是背定义。

## 4. 一个进程的全部家当：内存布局

汇编语言课本会给出进程地址空间的经典版图，这是理解"内存管理"的总地图：

plain

```plain
高地址 0x7fff...
┌─────────────┐
│   栈 stack    │  向下增长，局部变量、返回地址
├─────────────┤
│      ↓ 空闲 ↑  │  mmap / 堆扩展的空间
├─────────────┤
│   堆 heap     │  向上增长，malloc 从这里分配
├─────────────┤
│  BSS 段       │  未初始化的全局变量（不占文件空间）
│  数据段 data   │  已初始化的全局/静态变量
│  代码段 text   │  你的指令（只读、可执行）
低地址 0x400000
```

注意两个细节：

- **栈向下长，堆向上长**——因为不知道各自要用多少，让它们对着长、碰到一起就栈溢出。
    
- `objdump -d` 看代码段，`readelf -S` 看段表，符号地址是编译期就钉死的——这就是"静态区"。
    

## 5. 编译器如何把"程序"翻译成这套机制

这是学汇编最大的红利：把一段 C 代码编译成汇编对照着读（`gcc -S` 或 Compiler Explorer 网站）：

c

```c
int add(int a, int b) { return a + b; }
```

对应（x86-64，简化）：

asm

```asm
add:                    # a 在 %edi，b 在 %esi（ABI 规定参数放哪）
    lea  (%rdi,%rsi), %rax   # 结果放进 %rax（返回值也规定了放这）
    ret
```

你会亲眼看到：

- **调用约定（Calling Convention）**：参数放哪些寄存器、多余的压栈、返回值放 `rax`、谁负责清栈。函数调用协议只是"大家约定好的规矩"。
    
- **对齐（Alignment）**：`sub $16, %rsp` 这类操作出现的原因——SSE 指令要求 16 字节对齐，所以栈要保持对齐。很多神秘的段错误就是错在这里。
    

## 6. 动手验证清单

学汇编学内存，一定要"看真家伙"而不是空想：

1. **gdb 大法**：`gdb ./a.out` 后 `x/32xb $rsp`（看栈顶 32 字节）、`x/i $rip`（看当前执行的指令）、`info registers`。配合断点单步，内存在你眼前变化。
    
2. **Compiler Explorer**（godbolt.org）：左边写 C，右边看汇编，还能高亮哪条 C 对应哪几条汇编。
    
3. **故意搞破坏**：写个越界写数组的程序，用 gdb 观察它是怎么踩掉返回地址、导致 `ret` 跳飞的——你会真正理解缓冲区溢出和为什么需要金丝雀（canary，`%fs:0x28`）。
    
4. **大小端**：用 `mov` 读写一个 int，逐字节看内存里的顺序（x86 是小端），理解网络字节序为什么这么烦。
    

## 心智模型总结

> 汇编视角下，内存管理 = **一套地址约定**：寄存器（rsp/rbp）划出栈、malloc 划出堆、链接器划出静态段、OS 划出整个虚拟地址空间。高级语言的所有内存语义，都是这些约定的语法糖。

掌握到什么程度算过关？看到一个 C 的函数调用，脑子里能自动放出它的汇编：参数进哪些寄存器、栈帧怎么展开、返回值怎么拿。到这一步，你再回头看 GC、虚拟内存，都是顺水推舟。

需要的话，我可以给你出一个"用汇编手写递归函数"的实操练习，带你一步步建栈帧——这是检验理解最直接的方式。

x86 指令编码怎么算长度，链接器如何修正地址，什么是重定位表

Edit

Copy

Share

Used 4 tools, Calculating Relative Call Target Address

Demonstrate Relocation Entries via C/ASM Compilation and Inspection

User asks about x86 instruction encoding length calculation, how linker fixes addresses, what is relocation table. Educational answer in Chinese. This is a classic CS question — ELF relocation, R_X86_64_PLT32/PC32, instruction encoding: prefixes, opcode, ModRM, SIB, displacement, immediate.

Could demonstrate with tools? I could use ipython to run objdump/readelf if available. That might be nice — actually the environment may have gcc/readelf. Let me try a quick demonstration: write a small C/asm file, compile with gcc -c, objdump -d, readelf -r. That would be concrete. But no network for ipython, that's fine.

Let me do it: create test.c, gcc -c, objdump -d test.o, readelf -r test.o. This shows relocation entries. Let's try.

这是个非常好的进阶问题——它把"指令怎么编码"和"链接怎么把两块拼图合上"串起来了。我先在沙箱里给你做一个真实的实验演示，再讲原理。

Calculating Relative Call Target Address

Execute Python code

Execute Python code

Execute Python code

Thinking complete

Now link and show the final patched bytes.

Execute Python code

Thinking complete

Now write the explanation connecting everything. Show the math for the call: e8 d6 ff ff ff = 0xffffffd6 = -42 = target (0x401106) - (next instruction 0x401130) - 4.

实验做完了，上面的输出就是这三个问题的完整答案。下面把原理串起来讲。

## 一、x86 指令编码怎么算长度

x86 是**变长指令集**，一条指令由最多 5 个部分组成：

plain

```plain
[前缀 prefixes] [操作码 opcode] [ModRM] [SIB] [位移 displacement] [立即数 immediate]
   0~4 字节      1~3 字节      0/1字节  0/1字节   0/1/2/4/8 字节     0/1/2/4 字节
```

长度计算的规则（从上往下逐项判断）：

1. **前缀**（可选）：如 `0x48`（REX.W，表示操作 64 位）。注意 `48 89 e5` 里 `48` 就是 REX 前缀。
    
2. **操作码**：1~3 字节。单字节如 `55`（push rbp）；遇到 `0F xx` 是双字节操作码；带 `+r` 的会把寄存器编号编进操作码低位。
    
3. **ModRM**（可选）：由操作码决定是否带。一个字节分三段：**mod(2位) | reg(3位) | rm(3位)**。
    
    - mod=11 → 操作数是寄存器，没有后续；
        
    - mod=00/01/10 → 操作数是内存，按 rm 的值决定是否跟 SIB 和多少字节的位移。
        
4. **SIB**（可选）：当 ModRM 的 rm=100 时出现，提供 `base + index×scale`。
    
5. **位移 / 立即数**：长度由 ModRM.mod 和操作码决定。
    

**手工算一个**：上面实验里 `mov %edi,-0x4(%rbp)` 编码为 `89 7d fc`：

- `89` = opcode（mov r32 → r/m32）
    
- `7d` = ModRM：`01 111 101` → mod=01（跟 1 字节位移）、reg=111（edi）、rm=101（rbp）
    
- `fc` = 位移 -4
    

总长度 3 字节。看反汇编输出左边那一列地址（0, 1, 4, 7, d...）就是每条指令累加出来的长度——**CPU 解码器做的就是这件事：读一条、算出长度、前进、再读下一条**。

实用技巧：遇到指令解码问题，查 Intel 手册 Volume 2 的 opcode map；或者用 `objdump -d` 反汇编字节来验证你的手工解析。注意陷阱：x86 允许**从指令中间开始解释出不同指令**（动态反汇编时常见，比如跳转落到另一条指令的第 2 个字节）。

## 二、重定位表（Relocation Table）是什么

上面的实验里，`my_func` 和 `main` 编译在**同一个文件**里，但汇编器仍然给 `call` 填了 `e8 00 00 00 00`——一个假地址，旁边记了一条：

plain

```plain
26: R_X86_64_PLT32   my_func-0x4
```

这就是**重定位表**：一张"待填坑清单"。因为编译单个 `.c` 文件时，汇编器不知道：

- 别的文件里定义的符号（`ext_var`）最终在哪；
    
- 自己的代码链接后会被加载到哪个基址；
    
- 引用的全局变量（`internal_var`）在最终可执行文件里的确切地址。
    

所以它采取"**先留空 + 记账**"策略。每条记录（`readelf -r` 的每一行）包含四要素：

Table

|字段|含义|本例|
|:--|:--|:--|
|**Offset**|需要修补的位置（在本节内的偏移）|`0x26`：call 的立即数字段|
|**Type**|重定位类型（怎么算）|`R_X86_64_PLT32` / `PC32`|
|**Symbol**|引用了符号表（.symtab）里的谁|`my_func`|
|**Addend**|修正值（常为 -4，即"下一条指令地址"与字段起点的差）|`-0x4`|

注意符号表里 `ext_var` 的状态是 **UND**（未定义）——这就是留给链接器去别的 .o 或库里找的"欠条"。

## 三、链接器如何修正地址

链接分两步：**符号解析**（找到所有 UND 符号的真实定义）+ **重定位**（填坑）。

重定位的算法由类型决定。最常用两种：

**① R_X86_64_PC32 / PLT32（PC 相对寻址，call 和数据访问都是它）**

填写的不是绝对地址，而是**相对下一条指令的偏移**：

plain

```plain
要写入的值 = 目标地址 - 待修补字段的地址 + Addend
           = S + A - P
```

用实验里的 `call` 验证（链接后地址）：

- P = `0x40112c`（call 立即数字段的地址）
    
- S = `0x401106`（my_func 的地址）
    
- A = -4
    

plain

```plain
0x401106 - 0x40112c - 4 = -0x2a = 0xffffffd6
```

而 `e8 d6 ff ff ff` 的立即数正是 `0xffffffd6`（小端）✓。CPU 执行 call 时：rip 先指向 `0x401130`（下一条指令），加上 `0xffffffd6`（即 -42）= `0x401106`，跳中。

为什么用 PC 相对而不是绝对地址？**位置无关**：整个 .so 被加载到任意基址时，代码内部互相调用不需要任何修改。

**② R_X86_64_32 / 64（绝对地址，如全局变量指针表）**

plain

```plain
要写入的值 = S + A
```

链接器把每个节的符号地址都算好（按.text、.data 等顺序排布、对齐），再逐条遍历重定位表填数。**填完坑，重定位表本身就被丢弃了**——所以最终可执行文件里 `readelf -r` 通常看不到这些条目（除非还有动态重定位，那是动态链接器 ld.so 在加载时用同样算法再填一次，比如 PLT/GOT）。

## 心智模型总结

> - **指令长度**：前缀 + 操作码 + ModRM(+SIB+位移) + 立即数，逐项判断。
>     
> - **编译器/汇编器**：不知道最终地址 → 填 0 占位 + 写重定位表记账。
>     
> - **链接器**：把所有 .o 排好地址 → 按表填坑，公式 = 目标地址 - 修补点地址 + 修正值。
>     
> - **加载器**（动态链接时）：再来一遍，所以有了 GOT/PLT 这个缓存层。
>     

最好的巩固方式：自己写一个引用外部变量的程序，`objdump -d -r` 看占位、`readelf -r` 看表、链接后再 `objdump` 验证算出来的值——就像上面这个实验一样。需要我把这几个命令整理成一份可复现的完整实验脚本发给你吗？
实验脚本已经写好并测试通过。两个文件：

- **`relocation_lab.sh`** — 一键可复现实验：编译 → 反汇编看占位 → 查看重定位表和符号表 → 链接 → 验证填坑结果 → 观察动态重定位（GOT/PLT），每步都带观察要点提示。
    
- **`relocation_lab_README.md`** — 速查卡：实验流程表、重定位记录四要素、填坑公式 `S + A - P`、常见重定位类型速查，以及三个进阶练习（改 bss、函数指针、编成 .so 看 GOT/PLT）。
    

建议的运行顺序：先直接跑一遍脚本看输出，再对照 README 里的知识卡逐条验证，最后做进阶练习。跑的过程中有任何输出看不懂的，随时发给我。

#!/bin/bash # ============================================================================ # x86 指令编码 · 重定位表 · 链接器地址修正 —— 可复现实验脚本 # 运行环境：Linux x86-64，需要 gcc / binutils (objdump, readelf, nm) # 用法：bash relocation_lab.sh # ============================================================================ set -e cd "$(dirname "$0")" WORK=lab_tmp rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK" echo "################################################################" echo "# 第 1 步：编译两个源文件为目标文件（.o），暂不链接" echo "################################################################" cat > main.c <<'EOF' #include <stdio.h> extern int ext_var; // 定义在另一个文件里 → 符号表中是 UND static int internal_var = 42; // 本文件私有 → LOCAL 符号，不出现在最终动态符号表 int my_func(int x) { return x + internal_var + ext_var; } int main() { printf("result = %d\n", my_func(1)); return 0; } EOF cat > ext.c <<'EOF' int ext_var = 7; // 供 main.c 引用的外部定义 EOF # -fno-pie -c : 生成非位置无关的目标文件，地址更好观察 gcc -fno-pie -c main.c -o main.o gcc -fno-pie -c ext.c -o ext.o echo ">>> 已完成：main.o ext.o" echo echo "################################################################" echo "# 第 2 步：反汇编 .o —— 观察指令编码 + 占位的 0x00000000" echo "# 说明：-r 选项同时显示重定位记录" echo "################################################################" objdump -d -r main.o echo echo ">>> 观察要点：" echo ">>> 1) 每行开头的 16 进制字节就是指令的机器码，左边列是累加的偏移（=指令长度）" echo ">>> 2) mov 0x0(%rip) / call 后面跟着 R_X86_64_xxx 条目，立即数全是 00 00 00 00" echo echo "################################################################" echo "# 第 3 步：查看重定位表（待填坑清单）" echo "################################################################" readelf -r main.o echo echo "################################################################" echo "# 第 4 步：查看符号表 —— 理解重定位记录如何引用符号" echo "################################################################" readelf -s main.o echo echo ">>> 观察要点：" echo ">>> 1) ext_var 的 Ndx 是 UND（未定义）—— 欠条，等链接器去别处兑现" echo ">>> 2) Info 列的 000300000002 = 符号编号(高32位) | 类型(低8位)" echo ">>> 如 000300000002 → 符号 #3，类型 2 (R_X86_64_PC32)" echo echo "################################################################" echo "# 第 5 步：链接 —— 链接器解析符号 + 填坑" echo "################################################################" gcc -fno-pie -no-pie main.o ext.o -o demo echo echo "################################################################" echo "# 第 6 步：链接后再反汇编 —— 占位值已被填上真实偏移" echo "################################################################" objdump -d demo | sed -n '/<my_func>:/,/ret/p' objdump -d demo | sed -n '/<main>:/,/ret/p' echo echo "################################################################" echo "# 第 7 步：手工验证重定位公式 S + A - P" echo "################################################################" echo ">>> 以 call my_func 为例（R_X86_64_PLT32）:" echo ">>> 公式：填值 = 目标地址 S + Addend A (-4) - 修补点地址 P" echo ">>> 从上面的反汇编读出 S 和 P，代入验证字节是否等于 0xffffffd6 之类的偏移" echo echo ">>> 辅助：用 nm 查看最终符号地址" nm demo | grep -E 'my_func|ext_var|internal_var' || true echo echo "################################################################" echo "# 第 8 步（拓展）：动态重定位 —— GOT/PLT" echo "# 对比静态链接的死代码，观察共享库如何靠 GOT 二次填坑" echo "################################################################" readelf -r demo | head -20 echo echo ">>> 观察要点：可执行文件里仍保留 .rela.dyn / .rela.plt，" echo ">>> 这是留给动态链接器 ld.so 在加载时按同样公式再填一次的表。" echo echo "全部完成。工作目录: $WORK"

#  x86 重定位实验：指令编码 · 重定位表 · 链接器填坑

## 快速开始

bash relocation_lab.sh

依赖：`gcc`、`binutils`（objutils/readelf/nm），Linux x86-64。

## 实验流程

|   |   |   |
|---|---|---|
|步骤|命令|你要观察什么|
|1|`gcc -c`|只编译不链接，生成 `.o`|
|2|`objdump -d -r`|指令字节编码；`00 00 00 00` 占位；旁边的 R_X86_64_* 记录|
|3|`readelf -r`|重定位表：Offset / Type / Symbol / Addend 四要素|
|4|`readelf -s`|符号表：`UND` = 欠条，链接器要兑现|
|5|`gcc *.o -o demo`|链接：符号解析 + 按表填坑|
|6|`objdump -d demo`|占位值变成真实偏移，对照验证|
|7|手算|`S + A - P`，与机器码里的立即数比对|
|8|`readelf -r demo`|残留的动态重定位表（GOT/PLT），加载时由 ld.so 再填一次|

## 核心知识卡

****指令长度**** = 前缀(0~4) + 操作码(1~3) + ModRM(0/1) + SIB(0/1) + 位移(0/1/2/4/8) + 立即数(0/1/2/4)

****重定位记录四要素****

- Offset：待修补字段在本节内的偏移
- Type：怎么算（PC32/PLT32/32/64 ...）
- Symbol：符号表中的目标
- Addend：修正值，常取 -4

****链接器填坑公式（PC 相对）****

填值 = 目标地址 S + Addend A - 修补点地址 P

****典型类型速查****

- `R_X86_64_PC32` → rip 相对数据访问，小端 32 位偏移
- `R_X86_64_PLT32` → call/jmp 函数（经 PLT）
- `R_X86_64_32` → 绝对 32 位地址（如指针表项）
- `R_X86_64_64` → 绝对 64 位地址

## 建议的动手练习

1. 改 `internal_var` 为未初始化（`static int internal_var;`），对比 `.data` 与 `.bss` 段变化。
2. 在 `main` 里加一个函数指针调用，观察生成的重定位类型差异。
3. 用 `gcc -shared -fPIC` 编成 .so，再用 `readelf -r` 看 GOT/PLT 的二次填坑。