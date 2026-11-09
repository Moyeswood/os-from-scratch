# 里程碑 31：32 位汇编器与 DEBUG Monitor

## 汇编器

新增内核恢复 Shell 命令：

```text
asm source.asm -o /bin/program
```

汇编器采用两遍扫描：

1. 收集标签并计算每条指令/数据项大小；
2. 使用完整标签表生成机器码。

输出为入口 `0x02000000` 的 flat Ring 3 程序，可直接执行：

```text
run /bin/program
```

### 支持的语法

```text
label:
mov eax, 14
mov ecx, message
cmp eax, 14
jne bad
int 0x80
xor ebx, ebx
ret
message:
db "hello", 10
dd label
```

指令：

- `mov`、`lea`
- `add/or/and/sub/xor/cmp/test`
- `inc/dec/not/neg/mul/div`
- `shl/shr`
- `push/pop`
- `jmp/call/ret`
- `je/jne/jl/jle/jg/jge/jb/jbe/ja/jae`
- `int`、`syscall`
- `nop/hlt`

寻址：

- 32 位寄存器；
- 十进制/十六进制立即数；
- 符号立即数；
- `[absolute]`；
- `[reg]`、`[reg+disp]`、`[reg-disp]`。

数据：

- `db number` / `db "string"`；
- `dd number/label`；
- 字符串转义 `\n \r \t \0 \\ \"`。

汇编错误在串口与屏幕报告行号和原因。源文件上限 16KB，输出上限 16KB。

## DEBUG Monitor

```text
debug /bin/program
```

支持：

```text
r                 寄存器/镜像信息
d [addr]          8×16 字节十六进制 + ASCII
u [addr]          反汇编 8 条指令
e addr b1 b2...   修改镜像字节
w                 写回原文件
g                 生成临时 /bin/.debugrun 并运行
q                 退出
?                 帮助
```

反汇编器可识别汇编器当前生成的主要指令，包括 32 位立即数、
ModR/M、短指令、`int`、相对 `call/jmp` 和 `jcc`。

## 测试

新增 `tools/test-asm.py`，21 项检查：

- 在 OS 内使用编辑器写汇编源；
- 两遍汇编、标签地址和 flat 文件生成；
- 条件跳转、数据段、syscall 输出和退出；
- 未知指令及行号诊断；
- Monitor 的 `r/d/u/g/e/w/q`。

用户程序直接由生成的 `/bin/helloasm` 输出 `ASM OK` 并正常退出。

## 当前限制

- 只支持汇编器列出的指令子集，不是完整 NASM/GAS。
- 暂不支持 ELF、重定位、宏、包含文件、段和符号对象。
- 内存寻址暂不支持 SIB index/scale 的寄存器索引形式。
- Monitor 的 `g` 是整程序运行，尚未实现真实单步 `t`、step-over `p`
  和 `INT3` 断点。
- Monitor 寄存器视图在程序运行前显示静态用户上下文，运行后不会自动
  捕获目标进程现场。
