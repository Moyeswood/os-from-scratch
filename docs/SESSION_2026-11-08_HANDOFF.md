# 2026-11-08 开发交接

## 今日完成

### 1. 里程碑 25：崩溃一致文件系统

提交：`bbb7419 feat: add crash-consistent filesystem`

- 文件系统临界区改为可睡眠 mutex，等待者使用 `TASK_WAIT_FS`。
- 增加 24 块阈值、约 1 秒周期的延迟回写。
- 元数据 redo journal：先提交日志，再更新 home blocks。
- 启动时自动重放已提交日志，未提交日志安全忽略。
- `rename` 支持覆盖文件和空目录。
- 增加硬链接、按字节增长/截断、`sync` 命令。
- 新增 `tools/test-fs-journal.py` 断电注入测试。

### 2. 里程碑 26：用户信号与默认 Ring 3 Shell

提交：`fbb28ad feat: add user signals and Ring 3 default shell`

- 新增 `SYS_SIGACTION/SYS_SIGPROCMASK/SYS_SIGRETURN`。
- 支持 pending/mask、handler、当前信号自动屏蔽和用户栈返回帧。
- handler 通过 naked 汇编跳板调用 `sigreturn`，恢复完整用户现场。
- fork 复制信号状态，exec 重置为默认动作。
- 正式镜像 `build/os.img` 默认进入 Ring 3 `sh:/>`。
- `build/os-kernel.img` 直接进入内核恢复 Shell `OS:/>`。
- 用户 Shell 退出或崩溃后自动回退到恢复 Shell。
- 新增 `user/sigtest.c`、`tools/test-signals.py`、
  `tools/test-boot-shell.py`。

### 3. 里程碑 27a：Shell 2.0 基础

- 按字符词法分析，支持单/双引号、反斜杠和引号内操作符。
- 支持 `$HOME/$PATH/$NEWOS/$$/$?` 展开。
- 支持顶层 `;` 命令序列和上一条命令状态。
- 支持 `2>`、`2>>`、`2>&1`。
- 新增 `env/true/false` 内置命令。
- 键盘与 pipe 阻塞读取支持 `EINTR`。
- 修复退出码 127 被 `WIFSTOPPED()` 误判为暂停的问题。
- 设计记录：`docs/MILESTONE_27_SHELL_V2.md`。

## 今日验证

以下套件在当前代码上通过：

```bash
python3 tools/test-shell.py
python3 tools/test-signals.py
python3 tools/test-boot-shell.py
python3 tools/test-disk.py
python3 tools/test-user.py
python3 tools/test-fd.py
python3 tools/test-splash.py
```

另外已重新生成默认 Ring 3 Shell 和内核恢复 Shell 截图：

- `note/splash-shell.png`
- `note/recovery-shell.png`

## 当前状态

- 分支：`main`
- 当前进度：里程碑 27a
- 内核镜像：227588 字节，低于 448KB 加载上限
- 默认启动：Ring 3 Shell
- 测试/恢复启动：内核 Shell
- 工作区在本次交接提交完成后应为干净状态

## 明天从这里继续：里程碑 27b

按以下顺序推进：

1. `*`/`?` 通配符展开，先支持当前目录和绝对目录前缀。
2. Tab 补全，先补全内置命令、`/bin` 名称和当前路径项。
3. 将 Shell 历史写入持久化 FS，启动时恢复最近记录。
4. 为阻塞 syscall 明确 `SA_RESTART`/重启策略。
5. 增加对应 Shell 2.0 回归并重新生成截图。

## 当前限制

- Shell 尚无通配符和 Tab 补全。
- 历史记录只在当前 shell 进程内保存。
- `EINTR` 目前覆盖键盘和 pipe 阻塞读取，其他阻塞 syscall 尚未统一。
- 文件系统 journal 只保护元数据，最近未回写的数据仍可能在断电时丢失。
- 尚未执行完整 `make test-suite`；明天先跑交接文档中的针对性套件，功能完成后再统一全量回归。
