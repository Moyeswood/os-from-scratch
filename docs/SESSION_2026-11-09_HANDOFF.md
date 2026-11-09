# 2026-11-09 开发交接

## 今日完成

### 里程碑 29：POSIX 错误码与脚本状态

- syscall ABI 失败改为负 errno，用户库转换为 `-1 + errno`。
- 新增 23 个 flat 程序中的 `errtest`，覆盖常见文件/fd/进程错误。
- `exit N`、`sh -c "exit N"` 和脚本 `exit N` 能传播到父 Shell `$?`。
- Shell 测试增至 155 项，fd+errno 回归通过。

### 里程碑 28：Shell 3.0

- 键盘增加左右/Home/End/Delete/Ctrl+R 控制字节。
- Shell 编辑器支持光标位置插入、退格、Delete、历史草稿恢复和
  增量反向搜索。
- 环境改为 16 项可变表，支持局部赋值、export/unset/set/env 和
  `NAME=value command` 临时环境。
- `$()` 命令替换支持嵌套、多行输出和单引号抑制。
- 未闭合引号、`$()` 和尾部反斜杠支持 `> ` 多行续行。
- `sh FILE` 脚本执行、`#` 注释行和 `sh -c CMD`。
- 扩展 Shell 测试从 101 增至 146 项。

### 里程碑 27b：交互式 Shell

- `*`/`?` 通配符展开，支持当前目录、相对前缀和绝对目录前缀。
- 通配结果按名字排序；引用、转义和单引号抑制展开。
- 未引用变量值执行字段拆分；无匹配模式保留原文。
- Tab 补全内置命令、`/bin`、当前目录及路径前缀目录。
- 唯一候选补全后自动追加空格或目录 `/`；多候选先补公共前缀。
- 历史持久化到 `/.sh_history`，内存保留最近 24 条，文件达到 4KB
  后按内存窗口重写。
- 上/下方向键回忆历史，新增 `history [N]`。
- 新增 `SYS_SIGACTION_FLAGS` 与 `signal_flags(..., SA_RESTART)`。
- `read/write/waitpid/getkey` 包装函数通过私有重启哨兵恢复阻塞调用。
- pipe 部分写不会因重启哨兵重复发送已写入数据。
- fork 复制 `signal_restart_mask`，exec 和 reset 清零。

### 基础设施修复

- 用户链接脚本在 `.bss` 后输出结束字节，使 `objcopy -O binary` 保留完整
  零填充静态存储。修复 Shell 增大后 flat 镜像尾部未映射导致的 #PF。
- 内核 `help` 从 43 行压缩为 10 行，保证 20 行字体下常用命令和游戏
  入口不会滚出屏幕。

## 验证

`make test-suite` 的全部套件均已通过。首次全量运行仅在旧版 43 行
`help` 下触发 `test-mario` 屏幕扫描失败；帮助文本压缩后重新运行
`run-attached-tests.py`，`test-preempt` 与 `test-mario` 全部通过。

关键结果：

- Shell：155 checks ALL PASS。
- 信号、SIGINT/SIGTERM、EINTR、`SA_RESTART`：17 checks ALL PASS。
- 用户态启动、fd、process、exec、ELF32、文件系统、磁盘、日志、编辑器、
  C 编译器、VM、allocator、调度器、字体、splash、游戏：ALL PASS。
- 内核镜像：245796 字节，低于 458752 字节加载窗口。
- 用户 Shell flat 镜像：32017 字节，包含完整 BSS 零填充。
- Shell 3.0 后内核镜像：258212 字节。
- Shell 3.0 后用户 Shell 镜像：44305 字节。
- 里程碑 29 后内核镜像：259204 字节。
- 里程碑 29 后用户 Shell 镜像：43889 字节。

## 当前限制

- 行编辑已支持光标移动和历史搜索，但无水平滚动；极长行依赖终端换行。
- Shell 环境可变，但不跨 Shell 重启持久化。
- 赋值右侧来自未引用变量时仍可能先被字段拆分。
- 命令替换输出上限 1KB，且非零退出状态尚未转换为 `$?`。
- 脚本 `exit` 状态已传播；跨行引号和续行仍未完全解析。
- errno 是单翻译单元静态值；复杂文件系统错误仍可能归并为 EIO。
- 通配符在用户态目录枚举上实现，没有独立 glob 模块。
- 文件系统锁等待不返回 EINTR；现有阻塞键盘、pipe 和 waitpid 已覆盖
  `SA_RESTART`。
- 最多四段管道；fd 上限 16；pipe 缓冲区 1KB；argv 内核上限 16。

## 下次建议：里程碑 30（Shell 控制流）

1. `&&`、`||`、`!` 和分组。
2. `if/for/while/case`。
3. `test`、函数和 positional parameters。
4. 命令替换状态与赋值字段语义。
5. 脚本续行和跨行引号。

## 关键文件

- `user/sh.c`：词法、字段/通配展开、补全、历史和行编辑入口。
- `user/lib.h`：syscall 包装与 `SA_RESTART` 重试。
- `kernel/proc.c` / `kernel/fd.c`：信号 action、重启掩码和阻塞队列。
- `kernel/user.c`：重启哨兵跨 syscall 层传播。
- `user/linker.ld`：flat 用户镜像 BSS 输出修复。
- `kernel/shell.c`：紧凑内核恢复 Shell 帮助。
