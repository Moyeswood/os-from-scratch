# 里程碑 27a：Shell 2.0 词法与重定向

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

## 尚未完成

- `*`/`?` 通配符展开；
- Tab 补全；
- 历史记录跨重启持久化；
- `SA_RESTART` 与完整 field splitting。
