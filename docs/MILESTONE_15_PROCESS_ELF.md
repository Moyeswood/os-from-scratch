# 里程碑 15：waitpid、初始参数栈与 ELF32

日期：2026-11-06

## 1. 目标

里程碑 14 已经能从文件系统加载 flat 镜像并执行 `exec`，但进程模型仍有
三个明显缺口：

```text
fork 后用户父进程没有 waitpid
用户程序无法获得 argc / argv / envp
可执行文件只能是没有段元数据的 flat binary
```

里程碑 15 补齐：

- `waitpid(pid/-1, &status)`；
- 精确的直接子进程关系和僵尸回收；
- detached 后台任务；
- `argc/argv/envp` 初始用户栈；
- ELF32 文件头与 PT_LOAD 解析；
- 文件段、数据段和 NOBITS/BSS 映射；
- 只读代码页。

## 2. 进程生命周期

新进程创建时记录：

```text
parent_pid = 当前进程 pid
```

后台命令：

```text
run spinner &
```

创建的是 detached 任务：

```text
parent_pid = 0
```

这样 Shell 永远不会退出也不会阻碍后台僵尸被清扫。

用户程序退出时只进入 ZOMBIE，不释放自己仍在使用的 CR3。父进程运行在
自己的地址空间时，`waitpid` 才能安全释放孩子的页表与物理页。

父进程在回收孩子前退出时，会先把仍存活的直接孩子标成 detached，避免
孩子继续引用一个即将消失的父 pid。

## 3. waitpid 语义

系统调用：

```c
int waitpid(int pid, int* status);
```

规则：

- `pid > 0`：只等待该直接子进程；
- `pid == -1`：等待任意一个直接子进程；
- `pid == 0` 或小于 `-1`：返回 `-1`；
- 没有匹配的直接孩子：返回 `-1`；
- 孩子仍在运行：当前父进程睡到 `WAIT_PROC`；
- 孩子退出：返回孩子 pid，退出码写入 `status`；
- `status == NULL`：允许只等待、不接收状态。

状态指针必须落在当前已映射用户页中。内核不会为了写一个状态值而替用户
触发缺页。

实现循环遵守睡眠竞态纪律：

```text
cli
  → 重查 ZOMBIE
  → 重查孩子是否仍存在
  → 仍存在且未退出: sleep_switch
sti
```

## 4. 僵尸清扫

旧逻辑会在每次 spawn 时清扫所有 ZOMBIE。若用户父进程正在等待孩子，
另一个后台 spawn 可能提前把该孩子释放。

新规则只清扫：

```text
detached 后台任务的 ZOMBIE
父 pid 不存在或父任务本身已是 ZOMBIE 的 ZOMBIE
```

只要直接父进程仍存活，孩子就保留到 `waitpid`。

## 5. 初始用户栈

用户程序入口按 cdecl 参数读取方式启动：

```text
[initial ESP + 0]   fake return address = 0
[initial ESP + 4]   argc
[initial ESP + 8]   argv
[initial ESP + 12]  envp
```

因此：

```c
void _start(int argc, char** argv, char** envp);
```

可以直接从 iret 进入。

栈顶部布局从高到低构造为：

```text
0x02400000
  argv strings
  envp strings
  argv[] pointers + NULL
  envp[] pointers + NULL
  fake return / argc / argv / envp
initial ESP →
```

字符串先按字节复制到新地址空间；页尚未映射时由 VM 层分配零页。最后
写入指针数组和参数块。

Shell：

```text
run /bin/elfdemo alpha beta
```

得到：

```text
argc = 3
argv[0] = /bin/elfdemo
argv[1] = alpha
argv[2] = beta
```

默认环境：

```text
HOME=/
PATH=/bin
NEWOS=1
```

`execve` 会从旧用户地址空间复制用户提供的字符串和指针数组到临时内核
缓冲区，再构建新栈。旧地址空间销毁后，参数内容仍然存在。

限制：

- 最多 16 个 argv；
- 最多 8 个 envp；
- 参数文本总存储上限 3KB。

## 6. ELF32 文件验证

加载器识别：

```text
0x7F 'E' 'L' 'F'
```

当前支持：

| 字段 | 要求 |
|---|---|
| Class | ELF32 |
| Data | little-endian |
| Type | ET_EXEC |
| Machine | EM_386 |
| Entry | 用户镜像范围内且已映射 |
| PT_LOAD | 一至多个 |

拒绝：

- 错误的 magic/class/data；
- PIE/ET_DYN；
- 非 i386；
- program header 超出文件；
- `filesz > memsz`；
- 段越出用户镜像区；
- 段文件范围越界；
- 没有 PT_LOAD；
- 入口未映射。

## 7. 段与 BSS

每个 PT_LOAD：

```text
page_align(p_vaddr) .. page_align(p_vaddr + p_memsz)
```

先保证目标页存在且清零，然后复制文件内容：

```text
p_offset .. p_offset + p_filesz
```

`p_memsz - p_filesz` 对应 NOBITS/BSS，保持零值。

如果 `PF_W` 未设置，复制完成后清除 PTE 的 `PAGE_WRITE`，代码段只读。
fork 时：

- 可写页：共享并标 COW；
- 只读页：共享且保持只读；
- 两者都增加物理页引用计数。

## 8. 示例镜像

`user/elf_linker.ld` 显式生成两个 LOAD 段：

```text
PT_LOAD R E   .text .rodata
PT_LOAD RW    .data .bss (NOBITS)
```

`elfdemo.c` 使用：

- 已初始化数据 `initialized = 7`；
- 8KB BSS 缓冲区；
- `argc/argv/envp`；
- 校验值与 BSS 全零。

构建：

```bash
make build/elfdemo.elf
readelf -l build/elfdemo.elf
```

系统启动后把该 ELF 嵌入并发布到：

```text
/bin/elfdemo
```

## 9. 运行

```text
OS:/> run waittest
OS:/> run /bin/elfdemo alpha beta
OS:/> run execargv
```

`execargv` 使用：

```c
execve("/bin/elfdemo",
       (char*[]){"/bin/elfdemo", "from-exec", "env-test", 0},
       (char*[]){"ORIGIN=exec", 0});
```

## 10. 自动化验证

```bash
make test-process
```

39 项测试覆盖：

- 指定 pid 与任意子进程 `waitpid`；
- 42/7 两个退出码；
- 无匹配孩子和非法状态指针；
- 后台父进程存活时孩子僵尸不被 spawn 清扫；
- detached 后台父进程最终仍可回收；
- ELF 两段 PT_LOAD 装载；
- 已初始化数据值；
- 8KB BSS 清零；
- 默认 envp 与 Shell argv；
- `execve` 自定义 argv/envp；
- PID 保持；
- flat 文件兼容；
- 所有进程页在测试后回到预热基线。

## 11. 当前限制

- 不支持动态链接、PT_INTERP 和重定位；
- 不支持 ET_DYN/PIE；
- 不验证 ELF section header；
- 参数文本总量仅 3KB；
- 没有信号和 `waitpid` 的 WNOHANG 选项；
- 文件系统仍是 1MB 内存 FS，单文件最大 48KB。

下一阶段适合实现：

```text
open/read/write/close
  → 用户态 Shell
  → 管道与重定向
  → 更完整的进程状态和信号
```
