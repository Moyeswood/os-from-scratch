## 🎯 为什么现在必须建 git

说实话，我们之前用 `cp -r kernel kernel_backup_coop` 这种"土法备份"，在昨天那种连环崩溃的调试里已经暴露出问题了——**一旦改坏了，根本不知道是哪一步改坏的**。

有了 git，你会获得三个超能力：

1. **时间机器**：随时 `git checkout` 回到任何一个能跑通的版本
2. **增量试错**：每加一个小功能就 commit 一次，崩了只回退一步
3. **清晰的历史**：以后回头看，能清楚看到"第 X 次 commit 实现了抢占式多任务"

> 💡 顺便说一句：Linus Torvalds 写 git 的初衷，就是为了管理 Linux 内核。你正在做的，就是 Linux 当年走过的路。

---

## 🛠️ 第一步：创建 `.gitignore`（关键！）

OS 项目有很多特殊的二进制产物，**绝对不能提交到 git**。先建好这个文件：

bash


cat << 'EOF' > .gitignore
# 编译产物
build/
*.o
*.bin
*.elf
*.img

# 备份目录（昨天那种土法备份）
kernel_backup_*/
*.backup

# 编辑器/系统文件
.vscode/
.idea/
*.swp
.DS_Store
Thumbs.db

# GDB 历史
.gdb_history
EOF

echo "✅ .gitignore 已创建"



---

## 🚀 第二步：初始化仓库 + 第一个 commit

bash


# 初始化 git 仓库
git init

# 配置用户信息（如果还没配过）
git config user.name "Your Name"
git config user.email "your@email.com"

# 添加所有文件并提交
git add .
git commit -m "feat: 完成协作式多任务 (Cooperative Multitasking)

里程碑：
- Bootloader + 保护模式切换
- 物理内存管理 (位图分配器)
- 分页机制 (恒等映射 8MB)
- IDT + 8259A PIC 中断系统
- 键盘驱动 + Shell 交互
- 协作式多任务 (task A/B 交替打印 ABABAB)
- GDB 远程调试支持"

echo "✅ 第一个 commit 完成！"


---

## 📊 第三步：看一眼你的"时间线"

bash

git log --oneline

你会看到类似：

a1b2c3d feat: 完成协作式多任务 (Cooperative Multitasking)

这就是你的 OS 的**创世 commit**。从现在开始，每一次有意义的改动，都会成为这条时间线上的新节点。

---

## 🔄 推荐的工作流（极简版）

以后每做一件事，按这个节奏来：

bash

# 1. 改代码...
# 2. 跑通测试 (make run 看到预期效果)
# 3. 提交
git add .
git commit -m "feat(shell): 添加 clear 清屏命令"

# 4. 如果改坏了，回退到上一个能跑的版本
git checkout .
# 或者更狠一点：
git reset --hard HEAD



**commit message 的约定**（简单好用）：

- `feat(xxx):` 新功能
- `fix(xxx):` 修 bug
- `refactor(xxx):` 重构
- `docs:` 文档

比如今天做完 Shell 扩展，你的历史会变成：

e5f6g7h feat(shell): 添加 ps 命令查看任务
d4c3b2a feat(shell): 添加 mem 命令查看内存
a1b2c3d feat(shell): 添加 clear 命令 + 修复硬件光标
0987zyx feat: 完成协作式多任务 (Cooperative Multitasking)

**光看这个历史，就知道你这一路是怎么走过来的。** 这就是 git 的魔力。✨

---