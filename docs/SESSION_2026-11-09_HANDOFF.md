# 2026-11-09 开发交接

## 今日完成

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

- Shell：101 checks ALL PASS。
- 信号、SIGINT/SIGTERM、EINTR、`SA_RESTART`：17 checks ALL PASS。
- 用户态启动、fd、process、exec、ELF32、文件系统、磁盘、日志、编辑器、
  C 编译器、VM、allocator、调度器、字体、splash、游戏：ALL PASS。
- 内核镜像：245796 字节，低于 458752 字节加载窗口。
- 用户 Shell flat 镜像：32017 字节，包含完整 BSS 零填充。

## 当前限制

- Tab 只在行尾补全；尚无行内光标编辑、Home/End 和历史搜索。
- 上下键不保留尚未提交的历史浏览草稿。
- Shell 环境仍是只读默认 envp，尚无 `NAME=value`/`export`。
- 通配符在用户态目录枚举上实现，没有独立 glob 模块。
- 文件系统锁等待不返回 EINTR；现有阻塞键盘、pipe 和 waitpid 已覆盖
  `SA_RESTART`。
- 最多四段管道；fd 上限 16；pipe 缓冲区 1KB；argv 内核上限 16。

## 下次建议：里程碑 28（Shell 3.0）

1. 行内编辑、Home/End、历史搜索和草稿恢复。
2. 可变环境与 `NAME=value`/`export`。
3. `$(...)` 命令替换、多行输入和脚本文件执行。
4. 统一 POSIX 错误码与其余阻塞 syscall 的 EINTR 语义。
5. 增加 glob/Tab/历史的边界和压力测试。

## 关键文件

- `user/sh.c`：词法、字段/通配展开、补全、历史和行编辑入口。
- `user/lib.h`：syscall 包装与 `SA_RESTART` 重试。
- `kernel/proc.c` / `kernel/fd.c`：信号 action、重启掩码和阻塞队列。
- `kernel/user.c`：重启哨兵跨 syscall 层传播。
- `user/linker.ld`：flat 用户镜像 BSS 输出修复。
- `kernel/shell.c`：紧凑内核恢复 Shell 帮助。
