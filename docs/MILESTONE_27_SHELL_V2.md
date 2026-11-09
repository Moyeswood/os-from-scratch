# 里程碑 27：Shell 2.0

## 27a：词法与重定向

## 新增语法

```text
echo "hello world"        # 双引号
echo '$HOME'              # 单引号，不做展开
echo "$HOME"              # 环境变量展开
echo hello\ world         # 反斜杠转义
false; echo status=$?     # 命令序列与上一状态
nosuch 2> err.txt         # stderr 覆盖重定向
nosuch 2>> err.txt        # stderr 追加重定向
nosuch 2>&1 | cat         # stderr 合并到 stdout 后进管道
```

## 词法器

旧实现按空格和操作符直接切开输入。新实现按字符扫描：

- 单引号内完全保留字面值；
- 双引号内支持 `$VAR`，但不拆分字段；
- 引号外的反斜杠转义下一个字符；
- `$?` 展开上一条命令状态；
- `$$` 展开当前 shell pid；
- `$HOME/$PATH/$NEWOS` 从进程 envp 查找；
- 引号内的 `;`、`|`、`>` 不当作操作符；
- 顶层 `;` 被拆成顺序命令，任一步 `exit` 会终止 shell。

token 会写入独立静态缓冲区，避免变量展开覆盖原始输入。

## stderr 重定向

`command_t` 新增：

```text
error_output
error_append
stderr_to_stdout
```

重定向顺序：

1. stdin；
2. stdout；
3. `2>&1`；
4. 单独 stderr 文件，后者覆盖前者。

这样 `2>&1 | cat` 能正确捕获外部命令不存在时的错误。

## EINTR

新增用户态 `EINTR=4` 语义：

- 阻塞键盘读取检测待递送信号并返回 `-1`；
- pipe 读端无数据且收到可递送信号时返回 `-1`；
- syscall 返回前递送信号 handler。

Shell 当前不自动重启被中断命令，后续可增加 `SA_RESTART`/显式重启策略。

## 顺带修复

`WIFSTOPPED()` 原先只检查低 8 位是否等于 `0x7F`，会把正常的
退出码 127（command not found）误判为暂停。现在要求完整状态匹配
`(SIGSTOP << 8) | 0x7F`，避免错误作业残留。

## 测试

`tools/test-shell.py` 新增覆盖：

- 双引号、单引号、反斜杠；
- `$HOME` 与 `$?`；
- 引号内分号；
- 顶层命令序列；
- `2>`、`2>&1`；
- 原有管道、作业控制、Ctrl+C/Ctrl+Z 全部回归。

## 27b：交互式 Shell

### 通配符与字段展开

词法器为每个 token 单独记录“是否含有未引用 `*`/`?`”。展开分两阶段：

1. 未引用变量值中的空格按字段拆分；
2. 对含通配符的字段枚举目录、模式匹配、按名字排序，再把匹配项写回参数表。

单引号、双引号和反斜杠转义会抑制通配符；没有匹配时保留原模式，
与常见 Shell 的默认行为一致。支持当前目录、相对目录前缀和绝对目录前缀：

```text
echo *.txt
echo note.?d
echo /bin/sh*
echo "*.txt"             # 保持字面值
```

所有用户程序现在都有 `.bss` 尾部结束字节，`objcopy` 生成的 flat 镜像会
包含完整零填充静态存储，避免较大的 Shell 状态越过已映射镜像页。

### Tab 补全

Tab 在光标始终位于行尾的简化编辑模型中补全：

- 命令位置枚举内置命令与 `/bin`；
- 参数位置枚举当前目录或输入中的路径前缀目录；
- 单词匹配时补全剩余字符并追加空格，目录追加 `/`；
- 多个候选先补公共前缀，无公共扩展时列出候选。

内置命令和路径项共用一个 64 项、2KB 的静态候选池，不依赖动态分配。

### 持久化历史

命令提交前写入 `/.sh_history`，内存保留最近 24 条，文件超过 4KB 时
按内存窗口重写。上下方向键在内存历史中导航；`history [N]` 显示最近
N 条。退出并重新启动用户 Shell 后会重新加载文件，因此历史可跨 Shell
生命周期和重启保留。

### SA_RESTART

新增 `SYS_SIGACTION_FLAGS` 与 `signal_flags(sig, handler, SA_RESTART)`。
每个进程保存 `signal_restart_mask`；fork 复制、exec 清零。

阻塞 syscall 被带 `SA_RESTART` 的 handler 中断时，内核先返回私有
`SYS_RESTART` 哨兵，syscall 分发结束前递送 handler。handler
`sigreturn` 后，用户库的 `read/write/waitpid/getkey` 包装函数看到哨兵
并重试原调用。未设置 `SA_RESTART` 时仍返回 `-1`，调用者自行处理
EINTR。

已覆盖：

- 键盘和 pipe 的阻塞读写；
- `waitpid` 等待直接子进程；
- handler 运行后自动恢复 pipe 读和进程等待；
- 无 `SA_RESTART` 时 pipe 读返回 EINTR。

### 27b 测试

`tools/test-shell.py` 新增通配符排序、问号模式、引用抑制、绝对路径
通配符、内置命令/相对路径/绝对路径/目录 Tab 补全、上下键回忆以及
跨 Shell 重启历史。

`tools/test-signals.py` 新增 handler 运行后 `waitpid` 自动恢复、
pipe 读自动恢复和无 `SA_RESTART` 的 EINTR。

## 已知限制

- 不支持行内光标编辑；Tab 只在行尾补全。
- 历史没有搜索，固定为最近 24 条；交互编辑时不保留未提交草稿。
- 通配符匹配在用户态目录枚举上完成，没有独立的 glob/fnmatch 模块。
- `SA_RESTART` 重试包装覆盖当前所有会主动检查待递送信号的阻塞 syscall；
  文件系统锁等待仍优先完成事务，不返回 EINTR。
