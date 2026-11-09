# 里程碑 29：POSIX 错误码与脚本退出状态

## 设计目标

里程碑 29 保持旧用户程序的兼容行为，同时让新程序能区分错误原因：

```text
内核内部失败:        -1
syscall ABI 失败:    -ERRNO
用户库 API 失败:     -1，并设置 errno=ERRNO
```

这样：

```c
int fd = open("/missing", O_RDONLY);
if (fd == -1 && errno == ENOENT) {
    /* 文件确实不存在 */
}
```

而原有 `fd == -1`、`read(...) == -1` 等测试和程序无需修改。

## syscall 错误编码

内核新增常用 errno 常量，并保证系统调用不直接向用户返回裸 `-1`：

```text
EPERM ENOENT ESRCH EINTR EIO ENOEXEC EBADF ECHILD
EAGAIN ENOMEM EACCES EFAULT EBUSY EEXIST ENOTDIR EISDIR
EINVAL ENFILE EMFILE ENOSPC EPIPE ERANGE ENAMETOOLONG
ENOSYS ENOTEMPTY
```

`sys_encoded(result, fallback)` 将内核内部 `-1` 映射为当前 syscall
对应的负 errno；已经返回 `-ERRNO` 的路径保持原值。

## 用户库兼容层

`user/lib.h` 提供进程内 `errno` 和 `syscall_status(raw_result)`。
它在原始返回值属于 `[-4095, -1]` 时设置 errno 并返回 `-1`。
指针接口 `brk/mmap` 失败统一返回 `(void*)-1`；`getcwd` 失败返回 `NULL`。

`SYS_RESTART` 私有哨兵仍在 errno 转换之前处理，保持 `SA_RESTART`
自动重试语义。

## 已区分的关键错误

- 文件不存在：`ENOENT`
- 打开目录：`EISDIR`
- 在普通文件上遍历目录：`ENOTDIR`
- 无效 fd：`EBADF`
- 用户指针越界：`EFAULT`
- 创建已存在目录：`EEXIST`
- `getcwd` 缓冲区不足：`ERANGE`
- pipe 无读端写入：`EPIPE`
- 不存在的进程：`ESRCH`
- 没有可等待子进程：`ECHILD`
- 未知 syscall：`ENOSYS`

## 脚本退出状态

`exit [N]` 现在保存 `shell_exit_status`：

- 交互 Shell 退出时返回 N；
- `sh -c "exit N"` 返回 N；
- `sh FILE` 中遇到 `exit N` 后返回 N；
- 父 Shell 可通过 `$?` 读取 7、9 等非零状态。

## 测试

新增用户程序 `user/errtest.c`，覆盖 20 个错误场景并打印 errno。
`tools/test-fd.py` 增加 21 项 errno 断言。

Shell 新增 `sh -c "exit 7"; echo $?` 和 `sh /exit.sh; echo $?`
退出状态回归。

当前结果：

- Shell：155 checks ALL PASS
- fd + errno：ALL PASS
- signals / SA_RESTART：17 checks ALL PASS
- exec、process、ELF32：ALL PASS

## 当前限制

- `errno` 是每个用户翻译单元内的静态值；目前用户程序均为单翻译单元。
- 部分文件系统失败仍使用通用 `EIO`，例如复杂 rename 失败。
- 命令替换的非零状态尚未影响赋值命令的 `$?`。
- 脚本仍不支持跨行引号和续行解析。
