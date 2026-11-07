# 里程碑 19：进程级文件描述符与每进程 CWD

日期：2026-11-07

## 目标

此前用户程序只能输出字符，不能像普通程序一样打开、读取和写入文件；文件系统 CWD 也是全局变量，多个进程 `chdir` 会互相影响。

本里程碑建立后续用户态 Shell、管道和重定向的基础：

1. 每个 PCB 增加 8 项文件描述符表；
2. `fd 0/1/2` 固定为标准输入、标准输出、标准错误；
3. 实现 `open/read/write/close/lseek`；
4. CWD 移入 PCB，实现 `chdir/getcwd/mkdir`；
5. 验证 fork 继承、exec 保留和父子 CWD 隔离。

## fd 表设计

```c
typedef struct {
    uint32_t inum;      // 文件 inode
    uint32_t off;       // 当前文件偏移
    uint8_t kind;       // NONE / STDIN / STDOUT / FILE
    uint8_t readable;
    uint8_t writable;
    uint8_t append;
} task_fd_t;
```

`task_t` 内保存 `task_fd_t fds[8]` 和 `cwd_inum`。

- `fork` 复制 fd 表和 CWD；
- `exec` 不重建 PCB，因此 fd 和 CWD 自然保留；
- 新进程从父任务继承当前 Shell 的 CWD；
- 任务槽回收时重置 fd 表和 CWD。

当前复制的是文件偏移本身，因此父子进程执行 fork 后各自维护独立游标。后续实现管道时需要改成“进程 fd 表 → 内核打开文件对象”，让多个 fd 共享同一偏移和引用计数。

## 文件系统增强

- 路径解析不再读取全局 CWD，而是从 `task_current()->cwd_inum` 出发。
- 路径缓冲和 `pwd` 分段缓冲改为栈上数据，避免多任务切换时共享静态缓冲。
- 新增 `fs_write_at`，支持任意偏移写入。
- 新增 `fs_truncate/inode_type/inode_size` 查询与截断接口。
- 删除目录时检查所有任务的 CWD，而不只是当前 Shell。

## 系统调用

| 调用 | 参数 |
|---|---|
| `open(path, flags)` | `O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND` |
| `read(fd, buf, count)` | 文件或 fd 0；读写缓冲逐页校验 |
| `write(fd, buf, count)` | 文件或 fd 1/2 |
| `close(fd)` | 关闭并清空 fd 槽 |
| `lseek(fd, offset, whence)` | `SEEK_SET/SEEK_CUR/SEEK_END` |
| `chdir(path)` | 修改当前任务 CWD |
| `getcwd(buf, size)` | 写回绝对路径 |
| `mkdir(path)` | 创建目录 |

文件读入用户缓冲时使用 `vm_write_user`，因此即使目标页来自 fork COW，也会先完成私有化再写。

## 验证

`user/filedemo.c` 覆盖：

- 创建目录并切换 CWD；
- 创建、写入、按偏移追加、回卷、读取和关闭文件；
- 非法 fd 与非法用户缓冲；
- fork 后子进程切换根目录，父进程仍保持 `/fdhome`；
- 子进程无法用相对路径读到父进程目录中的文件；
- `write(1/2, ...)` 标准输出与标准错误。

`user/fdstdin.c` 单独验证 `read(0, ...)` 能读取 `hello\n`。

`tools/test-fd.py` 当前 34 项断言全部通过。另外回归：

- 文件系统 43 项；
- 编辑器 67 项；
- Ring 3 36 项；
- 进程模型 39 项；
- exec 26 项。

Shell 在用户进程退出后仍为 `/`，证明 CWD 已从全局状态变为任务私有状态。

## 当前限制

- fd 上限为 8，文件偏移在 fork 后不共享；
- 尚无 `dup/dup2/fcntl`；
- 尚无管道、重定向、stdin/stdout 替换；
- `open` 只支持 flat 文件，不支持 O_EXCL、权限与 errno；
- 文件系统仍为 1MB 内存 FS。

## 下一步

里程碑 20：把内核 Shell 搬到用户态，加入最小 `sh`、`cat/ls/echo` 用户程序和参数解析，再基于 fd 表实现管道与重定向。
