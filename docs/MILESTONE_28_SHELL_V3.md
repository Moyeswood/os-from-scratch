# 里程碑 28：Shell 3.0

## 28a：行内编辑与可变环境

### 行内编辑

键盘驱动在现有上/下方向键之外增加：

- 左/右方向键；
- Home/End；
- Delete；
- Ctrl+R。

它们使用非打印控制字节交给用户 Shell；编辑器等直接轮询扫描码的程序不受影响。

Shell 输入状态从“仅行尾追加”升级为：

```text
line[] + len + cursor
```

插入、退格和 Delete 都移动尾部内容；重绘时回到当前提示符起点，
覆盖旧输入并退格到目标光标。支持：

- 左右移动、Home、End；
- 光标位置插入与删除；
- Tab 在当前光标前的 token 上补全；
- 历史浏览时保存未提交草稿，向下越过末项后恢复；
- Ctrl+R 增量反向搜索，输入 query 时实时定位，Enter 接受，
  Esc/Ctrl+C 取消并恢复草稿。

### 可变环境

Shell 不再直接保存内核传入的 envp 指针，而是导入固定容量变量表：

```text
16 variables × 128 bytes
local / exported bit
```

支持：

```text
NAME=world
echo $NAME
export NAME
export FOO=bar
unset NAME
set
env
```

- 单独 `NAME=value` 是 Shell 局部变量；
- `export` 后才会进入外部程序 envp；
- `NAME=value command` 作为临时环境执行 command，父 Shell 随后恢复；
- 赋值右侧不做通配符展开，避免 `X=*` 修改文件集；
- 变量名限制为字母/下划线开头，后接字母、数字或下划线。

## 28b：命令替换、多行与脚本

### `$()` 命令替换

Shell 在词法分析前递归扫描 `$()`：

1. 支持括号嵌套与单/双引号上下文；
2. fork 子 Shell，将 stdout 接入 pipe；
3. 父 Shell 并发读取 pipe，避免 1KB pipe 阻塞；
4. 删除末尾换行后写回 1KB 展开缓冲区；
5. 继续执行普通变量展开、字段拆分和通配符展开。

单引号中的 `$()` 保持字面值。内部换行作为字段分隔符。

### 多行输入

提交行时检查未闭合单/双引号、尾部反斜杠和未闭合 `$()`。
不完整时显示 `> ` 续行提示，完整后合并执行；反斜杠续行会移除反斜杠。

### 脚本与 `-c`

```text
sh /script.sh
sh -c "echo command"
```

`sh FILE` 逐行读取脚本，跳过空行和 `#` 注释行，并在独立 Shell
进程中按普通命令序列执行。`sh -c` 直接执行一段命令。

## 测试

`tools/test-shell.py` 新增覆盖：

- 局部变量、export、env、临时环境和 unset；
- 左右/Home/End/Delete、历史草稿恢复和 Ctrl+R；
- 单层、多行、嵌套命令替换及单引号抑制；
- 多行引号输入；
- 脚本文件两行执行与 `sh -c`。

扩展后 Shell 套件为 101 → 146 项，全部通过。用户态键盘/echo、fd 和
默认 Ring 3 启动回归同时通过。

## 当前限制

- 环境不跨 Shell 重启持久化；
- 赋值 token 的右侧若来自未引用变量的空白，仍会先参与字段拆分；
- 命令替换输出上限 1KB，非零退出状态不会转换为 `$?`；
- 脚本不解析续行和跨行引号，`exit` 状态尚未传回脚本调用者；
- 行编辑没有可视水平滚动，极长行依赖终端自然换行；
- Ctrl+R 没有持久搜索历史或复杂 emacs 编辑键。
