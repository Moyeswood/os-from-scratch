# 里程碑 25：延迟回写与崩溃一致性

## 目标

消除里程碑 24 文件系统的三个核心风险：

1. 大文件 ATA I/O 期间长期关闭中断。
2. 所有修改都写穿，导致重复 FLUSH 和较差吞吐。
3. 断电时可能留下只更新了一半的 superblock、inode 或目录项。

## 可睡眠文件系统 mutex

`fs_lock()` 现在只在一个很短的窗口中关闭中断来获取锁：

- 当锁空闲时，当前任务成为唯一写者，随后立即重新开中断。
- 文件系统操作期间允许 IRQ0 抢占和任务切换。
- 第二个 FS 调用者进入 `TASK_WAIT_FS`，不再忙等。
- `fs_unlock()` 唤醒所有等待者，由调度器重新竞争。

嵌套公开 API 已拆成 `*_locked` 内部函数，避免同一任务重复获取 mutex。

## 有界延迟回写

- `FS_DIRTY_LIMIT = 24`：脏块达到 24 块时提交。
- `FS_FLUSH_TICKS = 18`：约一秒没有提交时，在空闲任务中周期 flush。
- `write_at()` 可累积文件数据；最后写者 `close()` 时强制 flush。
- 普通 `fs_write()`、创建、删除、rename 等高层操作在返回前提交。
- `reboot` 前自动 flush。
- 新增 `sync` 命令和 `df` 脏块统计。

数据块先写 home blocks；inode/目录等元数据随后通过日志更新。因此断电最多丢失最近文件内容，不会让有效元数据指向完全未写的数据块。

## 元数据 redo journal

磁盘前 1MB 原本保留未用，现在划分出：

```text
LBA 64          journal header (512 B)
LBA 80..       最多 120 个 4KB journal payload blocks
```

单次事务流程：

1. 写入无效 journal header 并 FLUSH。
2. 把新元数据块复制到 journal payload 并 FLUSH。
3. 写入 magic、sequence、block list 和 checksum，形成 committed header，并 FLUSH。
4. 把 journal payload 写回对应 home blocks，并 FLUSH。
5. 清除 journal header，并 FLUSH。

启动时：

- header 不完整或 checksum 错误：忽略，home blocks 保持旧版本。
- header 有效：幂等重放全部 payload，再清理日志。
- 重放完成后继续执行原有的块引用/位图结构验证。

## rename 覆盖

`rename(old, new)` 现在支持 POSIX 风格替换：

- 文件只能替换文件。
- 目录只能替换空目录。
- 根目录、任务 cwd/祖先以及非空目标目录会被拒绝。
- 同一目录内替换会先移除目标槽，再把源槽改名，避免重复目录项。
- 跨目录替换在同一元数据事务中完成，崩溃后要么看到旧状态，要么看到新状态。

## 硬链接与 truncate

- 新增 `fs_link()`、`SYS_LINK`、内核 `ln` 和用户态 `ln`。
- 删除一个硬链接只递减 `nlink`；最后一个链接删除时才释放 inode 和数据块。
- 新增 `fs_truncate_size()`、`SYS_TRUNCATE` 和 `truncate <file> N`。
- 收缩会释放尾部直接/间接块并清零保留块的尾端。
- 扩展会分配零块，并把原最后一块中的扩展区间清零。

## 测试

- `tools/test-fs2.py`：并发写、rename 覆盖、空目录替换、硬链接、truncate、用户态工具和重启持久化。
- `tools/test-fs-journal.py`：
  - 写入文件数据；
  - 提交 journal header；
  - 在写 home metadata 前直接终止 QEMU；
  - 用同一磁盘重启，验证 journal replay、文件存在、内容和后续 `sync`。

## 当前限制

- 日志只覆盖元数据；未 flush 的最近文件数据在断电时仍可能丢失。
- 单事务上限为 120 个元数据块。
- 尚无文件权限、时间戳和二级间接块。
- 文件系统仍是单卷、单 ATA primary-master 布局。
