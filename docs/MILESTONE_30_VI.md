# 里程碑 30：vi 风格 modal 编辑器

## 目标

在现有 [Editor](../kernel/editor.c) 的文本缓冲、撤销、搜索、文件读写和
VGA 渲染之上增加 vi modal 状态机，不复制文件编辑核心。

```text
edit FILE  -> nano 风格快捷键
vi FILE    -> Normal / Insert / Command 模式
```

新命令位于内核恢复 Shell。正式镜像启动的 Ring 3 Shell 中可先执行
`exit`，回到 `OS:/>` 后运行 `vi FILE`。

## 模式

### Normal

启动后进入 Normal：

```text
h j k l    左 下 上 右
0 $       行首 行尾
w b       下一个词 / 上一个词
G         文件末行
i a o     Insert / 追加 / 下方新行
x         删除光标字符
dd        删除当前行
yy        复制当前行
p         粘贴
u         undo
Ctrl+R    redo
/PATTERN  搜索
n         下一个匹配
:         命令模式
Esc       清除 pending 操作
```

同时支持方向键、Home/End、PgUp/PgDn 和 Delete。

### Insert

- 可打印字符插入文本；
- Enter、Backspace、Tab（4 空格）和 Delete 沿用编辑器语义；
- 所有连续输入进入同一个 undo group；
- Esc 返回 Normal，并按 vi 习惯将光标向左移一格。

### Command

```text
:w       保存
:q       未修改时退出，修改时阻止
:q!      丢弃修改并退出
:wq / :x 保存并退出
```

## 行操作

`yy` 和 `dd` 使用独立 4KB yank 缓冲，记录 linewise 属性。`p`：

- linewise 内容粘贴到当前行下方；
- charwise 内容粘贴到光标后；
- 粘贴内容进入独立 undo group。

`dw` 和 `d$` 作为附加 motion 一并支持。

## 状态栏

vi 模式显示：

```text
-- NORMAL -- / -- INSERT -- / -- COMMAND --
```

并保留行列、文件名和 `[M]` 修改标记。文件内容、撤销栈和搜索状态与
nano 模式完全共享。

## 验证

新增 `tools/test-vi.py`，28 项检查覆盖：

- `i/a/o` 与 Esc；
- 搜索和 `n`；
- `:w`、`:q`、`:wq`、`:q!`；
- `x`、`u`、`yy`、`p`、`dd`；
- 修改缓冲的 `:q` 阻止与 `:q!` 丢弃；
- 保存后的文件内容。

原 `tools/test-edit.py` 67 项全部通过，确认 nano 模式无回归。

## 当前限制

- 尚未支持 visual selection、blocks、registers、宏和 `:s` 替换。
- counter motions（如 `3j`）和 text objects（如 `ciw`）尚未实现。
- yank 缓冲上限 4KB，且只有单一 unnamed register。
- 只有内核恢复 Shell 提供 `vi`；用户态 Shell 尚无 modal 编辑器。
