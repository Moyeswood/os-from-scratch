# 2026-11-10 开发交接

## 当前状态

- 分支：`main`
- 远端：`origin/main`
- 当前进度：里程碑 31
- 内核镜像：279364 字节，低于 448KB 加载上限
- 用户 Shell 镜像：43889 字节
- 工作区在交接提交完成后应为干净

## 今日完成

### 里程碑 30：vi modal 编辑器

提交：`4518a6c feat: add vi modal editor`

- 新增内核恢复 Shell 的 `vi FILE`
- Normal/Insert/Command 模式
- `i/a/o`、Esc、`h/j/k/l`、`0/$`、`w/b`
- `x`、`dd`、`yy`、`p`、`dw`、`d$`
- `/`/`n`、`u`/Ctrl+R
- `:w`、`:q`、`:q!`、`:wq`、`:x`
- vi 测试 28 项；原 edit 测试 67 项

### 里程碑 31：汇编器与 DEBUG Monitor

提交：`ecd56d0 feat: add x86 assembler and debug monitor`

- `asm SRC -o DEST` 两遍扫描 flat 汇编器
- 标签、`db/dd`、字符串、立即数、寄存器、绝对和基址内存操作
- mov/lea、算术逻辑、栈、条件分支、call/ret、`int 0x80`
- 生成程序可从 `/bin` 直接运行
- `debug FILE` 支持 `r/d/u/e/w/g/q`
- 汇编程序 `ASM OK`、条件分支和 syscall 实际运行通过
- 汇编/DEBUG 联合测试 21 项
- 抢占/Mario attached 帮助屏幕回归通过

## 验证命令

```bash
make -j4
python3 tools/test-vi.py
python3 tools/test-edit.py
python3 tools/test-asm.py
python3 tools/run-attached-tests.py
```

今日关键结果：

- vi：28 checks ALL PASS
- edit：67 checks ALL PASS
- asm/debug：21 checks ALL PASS
- attached preempt/Mario：ALL PASS

## 明日继续顺序

### 第一项：里程碑 32 真实调试

1. 将待调试 flat 程序加载为暂停用户进程。
2. 保存并显示真实 EAX/EIP/EFLAGS/ESP 和页目录现场。
3. 使用 EFLAGS.TF + #DB 实现 `t` 单步。
4. 使用 `INT3/#BP` 实现软件断点、恢复指令原字节和命中报告。
5. 实现 `p` step-over call。
6. `g` 从暂停点/断点继续，而不是重新运行整个镜像。
7. Monitor 自动反汇编真实 EIP，显示栈顶和当前 CR2/CR3。

### 第二项：DEBUG 扩展

- `a addr` 交互式汇编
- `bp/list/clear`
- 实际寄存器修改
- 单步后的最近指令和寄存器变化

### 第三项：Shell 控制流

- `&&`、`||`、`!`
- `if/for/while/case`
- `test`、函数、positional parameters

## 当前限制

- 汇编器是自定义 32 位指令子集，不兼容完整 NASM/GAS。
- 尚未支持 ELF 输出、重定位、宏、include、section。
- SIB index/scale 寄存器索引寻址尚未实现。
- DEBUG 的 `g` 当前整程序运行，`r` 显示的是静态用户上下文。
- 尚无真实 `t/p/bp`、断点恢复和运行后寄存器捕获。
- `vi`、`asm`、`debug` 位于内核恢复 Shell；Ring 3 Shell 可先 `exit`。

## 关键文件

- `kernel/editor.c`：nano/vi 共享文本缓冲和 modal 状态机
- `kernel/assembler.c`：词法、两遍扫描、x86 编码和 flat 输出
- `kernel/debugger.c`：Monitor、内存转储、反汇编、运行
- `kernel/shell.c`：`edit/vi/asm/debug` 命令入口
- `tools/test-vi.py`
- `tools/test-asm.py`
- `docs/MILESTONE_30_VI.md`
- `docs/MILESTONE_31_ASSEMBLY_DEBUG.md`
