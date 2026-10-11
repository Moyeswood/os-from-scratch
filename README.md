# OS From Scratch 🖥️

> 从零开始实现一个简单的操作系统 | Learning OS development from scratch

## 📖 项目介绍

这是一个**教学项目**，通过从头开始实现一个微型操作系统，深入学习计算机底层知识。项目涵盖了：

- **汇编语言** - x86 实模式编程、中断处理、内存管理
- **C 语言** - 内核驱动开发、系统调用实现
- **编译原理** - 引导加载器、链接脚本、二进制格式
- **计算机原理** - CPU、内存、I/O、中断、虚拟内存

### 🎓 适合人群

- 想深入理解 OS 原理的学生
- 对底层系统编程感兴趣的开发者
- 正在学习编译原理或计算机体系结构的人

---

## 📁 项目结构

```
os-from-scratch/
├── bootloader/              # 引导加载器
│   ├── boot.asm            # MBR 引导代码
│   └── kernel_load.asm     # 内核加载器
├── kernel/                  # 内核代码
│   ├── kernel.c            # 内核主程序
│   └── drivers/            # 驱动程序
│       ├── disk.c          # 磁盘驱动
│       └── serial.c        # 串行通信
├── libc/                    # C 标准库实现
│   ├── stdio.c             # 输入输出
│   └── string.c            # 字符串处理
├── tools/                   # 构建工具
│   ├── build.sh            # 编译脚本
│   └── image.py            # 镜像生成工具
├── docs/                    # 文档
│   ├── architecture.md     # 系统架构
│   ├── bootloader.md       # 引导过程
│   └── memory_layout.md    # 内存布局
└── README.md               # 本文件
```

---

## 🚀 快速开始

### 前置要求

```bash
# Linux/macOS 安装依赖
sudo apt-get install nasm gcc binutils qemu   # Ubuntu/Debian
brew install nasm gcc binutils qemu           # macOS

# Windows 可使用 WSL2 或虚拟机运行 Linux
```

### 编译和运行

```bash
# 克隆项目
git clone https://github.com/Moyeswood/os-from-scratch.git
cd os-from-scratch

# 编译
./tools/build.sh

# 运行（使用 QEMU 虚拟机）
qemu-system-i386 -fda os.img

# 或者烧录到 USB 实际测��
# dd if=os.img of=/dev/sdX (慎用！)
```

---

## 📚 学习路线

### 第一阶段：引导加载器 (Bootloader)
- [ ] 理解 BIOS 启动过程
- [ ] 实现 MBR 引导代码
- [ ] 从磁盘加载内核
- [ ] 进入保护模式

**相关文件**：`bootloader/`  
**学习资源**：`docs/bootloader.md`

### 第二阶段：内核基础
- [ ] 设置 GDT（全局描述符表）
- [ ] 实现中断处理
- [ ] 简单的内存管理
- [ ] 打印输出功能

**相关文件**：`kernel/`

### 第三阶段：驱动程序
- [ ] 磁盘 I/O 驱动
- [ ] 键盘/鼠标输入
- [ ] 串行通信
- [ ] 文件系统基础

**相关文件**：`kernel/drivers/`

### 第四阶段：进程管理
- [ ] 进程/线程创建
- [ ] 上下文切换
- [ ] 调度算法
- [ ] 内存保护

### 第五阶段：用户程序
- [ ] Shell 命令行
- [ ] 系统调用接口
- [ ] 简单应用程序

---

## 💡 核心特性

### 已实现 ✅
- [x] MBR 引导加载器
- [x] 磁盘读取功能（BIOS int 0x13）
- [x] 跳转到内核代码

### 进行中 🔄
- [ ] 保护模式切换
- [ ] 中断处理器

### 计划中 📋
- [ ] 虚拟内存
- [ ] 多进程支持
- [ ] 文件系统
- [ ] 网络支持

---

## 📖 重要概念解释

### 实模式 vs 保护模式

| 特性 | 实模式 | 保护模式 |
|------|--------|---------|
| 寻址能力 | 1 MB | 4 GB |
| 内存保护 | ❌ | ✅ |
| 多任务 | 有限 | ✅ |
| 权限级别 | Ring 0 only | Ring 0-3 |
| 启动阶段 | BIOS 启动 | 引导加载器 |

### 中断和异常

```
中断（Interrupt）
├── 硬件中断（Hardware Interrupt）- 外设请求
│   ├── IRQ0 - 时钟
│   ├── IRQ1 - 键盘
│   └── ...
└── 软件中断（Software Interrupt）- int 指令

异常（Exception）
├── Fault - 可恢复错误
├── Trap - 陷阱（调试）
└── Abort - 不可恢复错误
```

---

## 🛠️ 开发指南

### 代码风格

**汇编代码**：
```assembly
; 使用有意义的标签和注释
function_name:
    push rbp                ; 保存栈指针
    mov rsp, rbp            ; 设置新栈帧
    ; 函数逻辑
    pop rbp
    ret
```

**C 代码**：
```c
// 遵循 Linux kernel coding style
int kernel_main() {
    printk("Hello, OS!\n");
    return 0;
}
```

### 提交规范

使用 Conventional Commits：
```bash
git commit -m "feat: add disk_load function"
git commit -m "fix: resolve stack imbalance in bootloader"
git commit -m "docs: add memory layout documentation"
```

### 测试方法

```bash
# 单元测试
make test

# 集成测试（QEMU 模拟）
make test-qemu

# 实机测试（USB 烧录）
make test-usb
```

---

## 📚 参考资源

### 书籍
- 《Orange'S：一个操作系统的实现》- 橙书
- 《Operating Systems: Three Easy Pieces》
- 《The Linux Kernel Module Programming Guide》

### 在线教程
- [OSDev.org - OS Development Wiki](https://wiki.osdev.org/)
- [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/index.php)
- [JamesM's Kernel Development Tutorials](https://www.jamesmolloy.co.uk/tutorial_html/)

### 文档
- Intel x86 Architecture Reference Manual
- AMD64 Architecture Programmer's Manual
- BIOS Boot Specification

---

## 🤝 贡献指南

欢迎提交 Issues 和 Pull Requests！

### 提交 Issue
1. 在 [Issues](../../issues) 页面点击 "New Issue"
2. 清晰描述问题或功能需求
3. 附加相关的错误信息或代码片段

### 提交 PR
1. Fork 本仓库
2. 创建功能分支：`git checkout -b feature/your-feature`
3. 提交改动：`git commit -m "feat: add your feature"`
4. 推送到分支：`git push origin feature/your-feature`
5. 提交 Pull Request

**PR 检查清单**：
- [ ] 代码遵循项目风格
- [ ] 添加了必要的注释
- [ ] 更新了相关文档
- [ ] 提交信息清晰明确

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源许可。

---

## 👨‍💻 作者

**Moyeswood**  
GitHub: [@Moyeswood](https://github.com/Moyeswood)

如有问题，欢迎通过 Issues 或邮件联系。

---

## 🎯 项目目标

通过实现这个 OS，学习者将能够：

✅ 理解计算机启动的全过程  
✅ 掌握 x86 汇编语言  
✅ 学习操作系统的核心概念  
✅ 了解硬件和软件的交互  
✅ 提升系统级编程能力  

---

**⭐ 如果这个项目对你有帮助，请给个 Star！**
