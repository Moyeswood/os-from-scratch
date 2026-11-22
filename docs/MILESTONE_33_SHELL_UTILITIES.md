# 里程碑 33：实用 Shell 命令与脚本工具

## 目标

在 Ring 3 用户 Shell 中补齐日常文件、文本和脚本操作，并改善已有命令的
可用选项。新增命令优先复用现有 syscall，只有用户态清屏增加了
`SYS_CLEAR=36`。

## 文件工具

```text
touch FILE...
cp [-r] SOURCE TARGET
mkdir -p PATH...
rm [-rf] PATH...
ls [-la] [PATH]
cd -                    # 返回并打印上一条目录
```

`cp` 支持普通文件、目标目录和递归目录复制；复制前检查源目标是否指向
同一 inode，避免硬链接自覆盖。`rm -r` 反复扫描目录项，避免边删除边按
递增索引遍历造成漏项。`mkdir -p` 逐级创建前缀目录。

## 文本工具

```text
head [-n N] [FILE...]
tail [-n N] [FILE...]
wc [-lwc] [FILE...]
grep [-in] PATTERN [FILE...]
hexdump [FILE...]
```

- 无文件名时读取 stdin，因此可直接接管道。
- `tail` 对可 seek 文件从文件尾反向定位，对 pipe 使用 16 行环形缓冲。
- `grep` 当前是固定字符串匹配，支持忽略大小写与行号。
- `wc` 支持多文件统计和总计。
- `hexdump` 每行显示 16 字节、偏移和 ASCII。

## 脚本工具

```text
printf FORMAT [ARG...]
test EXPRESSION
[ EXPRESSION ]
read NAME
source FILE
. FILE
```

`printf` 支持 `%s/%d/%u/%x/%c/%%` 与 `\n/\r/\t/\0`。`test` 支持：

- 文件：`-e/-f/-d`
- 字符串：`-z/-n/=/!=`
- 整数：`-eq/-ne/-lt/-le/-gt/-ge`
- 前缀 `!`

`read` 可从键盘或 `< FILE` 读取一行到 Shell 变量。`source`/`.` 在当前
Shell 进程执行脚本，因此变量、目录和退出状态会保留。

## 其他

```text
clear
sleep SECONDS
uptime
which COMMAND...
type COMMAND...
history -c
```

`clear` 新增一个最小的用户态清屏 syscall；`sleep` 按现有
18.2Hz PIT 滴答换算为近似秒数；`uptime` 读取启动以来滴答数。

## 内核 BSS 布局修复

Shell 镜像增长后，内核 BSS 超过 `0xA0000`，进入 VGA/BIOS 保留空间，
导致用户程序表读取得到 `0xFFFFFFFF`。链接脚本现在把 BSS 放到普通 RAM
的 `0x00200000`，内存分配器仍根据 `_kernel_end` 保留完整内核区域。

## 验证

`tools/test-shell.py` 扩展到 224 项检查，覆盖：

- 复制、递归复制、递归删除和父目录创建；
- head/tail/wc/grep 与管道；
- printf/test/read/source；
- clear/uptime/hexdump/which/type；
- `ls -a`、`cd -`、`history -c`；
- 原有引号、变量、命令替换、重定向、作业控制和历史持久化。

## 当前限制

- `grep` 是固定字符串匹配，尚无正则表达式。
- `tail` 的 stdin 环形缓冲最多保留 16 行。
- `printf` 尚无宽度、精度和格式复用语义。
- `test` 尚无 `-a/-o` 复合表达式和完整 POSIX 优先级。
- Shell 仍没有 `if/for/while/case` 控制流。
