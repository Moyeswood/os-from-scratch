# 🚀 NewOS

<p align="center">
  <em>A 32-bit x86 micro operating system, hand-written from scratch</em><br>
  <em>From a 512-byte boot sector to "write C, compile it, and run it — inside your own OS"</em>
</p>

<p align="center">
  <a href="README.md">中文</a> · English
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20x86%20ASM-4CAF50" alt="language"/>
  <img src="https://img.shields.io/badge/Milestones-11%2F11%20Done-blue" alt="milestones"/>
  <img src="https://img.shields.io/badge/Tests-284%20Passing-brightgreen" alt="tests"/>
  <img src="https://img.shields.io/badge/Platform-QEMU%20i386-orange" alt="platform"/>
</p>

<p align="center">
  <img src="note/splash-mid.png" width="480" alt="boot screen"/>
</p>

NewOS is an educational 32-bit x86 operating system written from scratch in C and a bit of assembly, running in QEMU. It covers the full OS learning path: **booting → protected mode → interrupts → memory management → multitasking → graphics → filesystem → editor → C compiler → Ring 3 user mode** — so that in the end you can write a C program with the built-in editor, compile it with the built-in compiler, run it, and also run user-mode processes and watch the kernel catch them red-handed when they go out of bounds.

## ✨ Features

### 🖥️ Core
- **Bootloader**: a hand-written 512-byte boot sector that loads the kernel from a floppy (150 KB limit, with 64 KB segment wrap-around handling)
- **Protected mode**: GDT segmentation, A20 gate, 32-bit mode switch
- **Memory management**: bitmap physical page allocator (including contiguous multi-page `alloc_pages`) + paging (32 MB kernel identity map + 4 MB user region)
- **Interrupts**: full IDT (32 exception handlers + IRQs + syscall gate), 8259A PIC remap, PS/2 keyboard interrupt driver
- **Real-mode round trips**: drop back to real mode to call the BIOS (video mode switching) and return to protected mode automatically — the trampoline saves/restores all callee-saved registers

### 🛡️ Ring 3 User Mode
- **Privilege isolation**: TSS kernel-stack switch, user code/data segments (DPL3), page-table U/S protection — a user program touching kernel memory gets a #PF
- **System calls**: `int 0x80` (DPL3 gate) — putc/puts/read_key/get_ticks/exit; the kernel validates that user-supplied pointers fall inside the user region
- **Exception handling**: 32 exception handlers print the full fault context (EIP/CS/EFLAGS/ESP/CR2 + page-fault error code P/W/U decoding); a user-mode fault kills the process and returns to the shell, a kernel-mode fault panics
- **6 built-in demo programs** (host-gcc compiled → incbin → loaded by the `run` command): `hello` (minimal user process), `count` (timer keeps ticking in user mode), `crash` (write to 0xB8000 → #PF), `div0` (divide by zero → #DE), `echo` (keyboard echo), `badptr` (syscall argument validation)

### ⏱️ Multitasking & Devices
- **Preemptive multitasking**: PIT IRQ0 (18.2 Hz) round-robin scheduling; cooperative and preemptive context frames are mutually restorable; includes a "bully task" demo
- **Device drivers**: VGA text/graphics, COM1 serial, PS/2 keyboard, PIT timer, PC speaker beep
- **Graphics library**: mode 13h (320×200×256) palette/pixel/rect/sprite/font
- **🎨 Adjustable fonts**: pure VGA hardware programming to load **Terminus** bitmap fonts (writing character-generator plane 2 + CRTC row reprogramming); `font` command with 5 sizes (12/14/16/18/20 → 33/28/25/22/20 lines, default 18; 18/20 vertically scaled from 8x16), hardware-level self-test on every load

### 📁 Filesystem
xv6-style teaching implementation (in-memory, 1 MB / 256 blocks, 64 inodes):
- Superblock + inode/block bitmaps + 64-byte inodes (12 direct blocks, max file 48 KB)
- Chained directory entries, real `.`/`..` entries, unified absolute/relative path resolution
- Shell commands: `pwd` `ls` `cd` `mkdir` `touch` `cat` `rm` `echo >` `df`, with the prompt showing the current directory
- Removal protection (cannot delete cwd or its ancestors), boot self-test (create → write → read back → delete, fully verified)

### ✏️ Built-in Editor
A nano-style `edit` command:
- Arrow keys / Home / End / PgUp / PgDn navigation, insert/backspace/delete/newline, Tab expansion
- **Ctrl+S to save, Ctrl+X to exit** (double confirmation when modified)
- Inverse-video status bar (filename / line,column / [M] marker), highlighted cursor cell, horizontal scrolling of long lines
- Polls the 8042 directly and tracks Shift/Ctrl/CapsLock/0xE0 extended-key prefixes itself

### ⚙️ C Compiler (c4 port)
The `cc` command — a port of **C in four functions** (by Robert Swierczek), compiling a C subset to a built-in stack virtual machine:

| Supported | Notes |
|---|---|
| Types | `char`, `int`, pointers, 1-D arrays, `enum`, `sizeof` |
| Declarations | May have initializers: `int a = 5;` (global & local; globals run before `main`) |
| Statements | `if/else`, `while`, `do/while`, `for`, `break/continue`, `return`, expressions, function calls (incl. recursion) |
| Comments | `//` and `/* */` |
| Builtins | `printf` `open` `read` `close` `malloc` `free` `memset` `memcmp` `exit` |
| Not supported | struct/union, float, switch, goto, preprocessor; avoid `&&` `\|\|` `?:` in for-increment / global-initializer expressions (deferred-codegen limitation) |

**`open/read` talk to the OS's own filesystem** — C programs can read files inside the OS! Compile errors include line numbers; runaway loops can be interrupted with ESC.

### 🎮 Fun & Applications
- **Tetris**: 7-bag randomizer, wall kicks, lock delay, hold piece, line-clear animation, 5 color themes, beep SFX
- **Super Mario**: a 16×16-tile side-scrolling platformer (scrolling camera + parallax background, 8.8 fixed-point physics, Goombas, coins/? blocks/pipes/stairs/flagpole)
- **🚀 Boot screen**: deep-blue gradient starfield (DAC palette programming) + 32px colorful "HELLO WORLD" letter-by-letter reveal animation + "NEWOS!" subtitle; press any key to skip; replayable via the `splash` command

## 💻 Command Reference

| Command | Description |
|---|---|
| `help` | List all commands |
| `clear` | Clear the screen |
| `meminfo` | Memory statistics (pages) |
| `echo [text]` | Print text; `echo text > file` writes (overwrites) a file |
| `date` | CMOS real-time clock |
| `version` / `reboot` | Version / reboot |
| `ps` / `start` / `stop` | Task status / start-stop the multitasking demo |
| `testmode` | Graphics-mode real-mode round-trip test |
| `tetris` / `mario` | Games (ESC to exit) |
| `font 12\|14\|16\|18\|20` | Font size (33/28/25/22/20 lines, default 18) |
| `pwd` | Print working directory |
| `ls [dir]` | List directory (`<DIR>` marks directories) |
| `cd [dir]` | Change directory (no arg = root) |
| `mkdir dir` | Create a directory |
| `touch file` | Create an empty file |
| `cat file` | Show file contents |
| `rm path` | Remove a file or empty directory |
| `df` | Filesystem usage |
| `edit file` | Open the editor (^S save, ^X asks to save on exit) |
| `cc file.c [args...]` | Compile and run a C program (args go to main) |
| `run prog` | Run a user program in Ring 3 (hello/count/crash/div0/echo/badptr) |
| `splash` | Replay the boot screen |

## 🚀 Quick Start

**Requirements** (Arch Linux): `nasm`, `gcc` (32-bit support), `ld`, `objcopy`, `qemu-system-i386`

```bash
make          # build build/os.img
make run      # run in QEMU (serial output on the terminal)
make debug    # QEMU + GDB source-level debugging (port 1234)
make clean    # clean up
```

After boot, a ~4 second boot screen plays (press any key to skip), then the shell:

```
OS:/> help
```

## 📝 Typical Workflow

```
OS:/> mkdir src
OS:/> cd src
OS:/src> edit hello.c          ← write a C program in the editor
OS:/src> cc hello.c            ← compile and run
hello from C! (42)
exit(0) cycle = 11
```

Even cooler: a C program reading the OS's own filesystem —

```c
int main() { int fd; char *buf; int n;
fd = open("/data.txt", 0); buf = malloc(32);
n = read(fd, buf, 31); buf[n] = 0;
printf("read %d: %s\n", n, buf); return 0; }
```

## 📷 Screenshots

| Filesystem operations | Built-in editor | C compile & run |
|---|---|---|
| <img src="note/shell-fs.png" width="240" alt="filesystem ops"/> | <img src="note/editor.png" width="240" alt="editor"/> | <img src="note/cc-demo.png" width="240" alt="cc"/> |

| Ring 3 user mode | User fault caught by the kernel |
|---|---|
| <img src="note/ring3-hello.png" width="360" alt="ring 3 user mode"/> | <img src="note/ring3-crash.png" width="360" alt="page fault"/> |

## 🧪 Automated Tests

8 serial-driven headless test suites (QEMU monitor key injection + kernel serial marker assertions), **233 checks in total**:

```bash
python3 tools/test-splash.py   # boot screen  (14 checks)
python3 tools/test-font.py     # fonts        (30 checks)
python3 tools/test-fs.py       # filesystem   (43 checks)
python3 tools/test-edit.py     # editor       (50 checks)
python3 tools/test-cc.py       # C compiler   (41 checks)
python3 tools/test-preempt.py  # preemption   (7 checks)
python3 tools/test-mario.py    # Mario        (12 checks)
python3 tools/test-user.py     # Ring 3 user  (36 checks)
```

> test-preempt / test-mario are attached tests (argv[1] = monitor socket);
> use `python3 tools/run-attached-tests.py` (boots QEMU → waits for the boot screen → runs both).

## 📂 Directory Layout

```
.
├── boot/boot.asm       # 512-byte boot sector
├── kernel/             # kernel source (C + asm)
│   ├── kernel.c        # kernel entry
│   ├── memory.c/h      # bitmap physical memory allocation
│   ├── paging.c/h      # paging (kernel identity map + user region)
│   ├── gdt.c/h         # kernel GDT (user segments + TSS)
│   ├── idt.c/h pic.c   # interrupts & 8259A
│   ├── irq.asm         # IRQ assembly stubs
│   ├── exc.c exc_asm.asm    # exception handlers (32 vectors)
│   ├── user.c/h user_asm.asm user_progs.c user_embed.asm  # Ring 3 user mode
│   ├── keyboard.c      # PS/2 keyboard
│   ├── timer.c/h       # PIT ticks + beeper
│   ├── vga.c/h         # text mode display
│   ├── graphics.c/h    # mode 13h graphics library
│   ├── font.c/h        # VGA character-generator font management
│   ├── font12/14/16.h  # Terminus font data
│   ├── fs.c/h          # in-memory filesystem
│   ├── editor.c/h      # built-in editor
│   ├── cc.c/h          # c4 compiler port
│   ├── splash.c/h .asm # boot screen
│   ├── tetris.c/h      # Tetris
│   ├── mario.c/h       # Super Mario
│   ├── task.c/h switch.asm   # multitasking
│   ├── realmode.c/h rm_*.asm  # real-mode round-trip trampoline
│   └── keymap.h string.h      # scancode maps / string utils
├── user/               # user-mode demo programs (host-gcc built, incbin'd into the kernel)
│   ├── lib.h           # syscall wrappers (int 0x80)
│   ├── linker.ld       # linked at 0x2000000 (user region)
│   └── hello.c count.c crash.c div0.c echo.c badptr.c
├── tetris/             # original real-mode assembly version (port reference)
├── tools/              # font converter + 8 test suites
├── note/               # development notes + screenshots
└── Makefile
```

## 📖 Project History

| Milestone | Content |
|---|---|
| 1 | Real-mode round trips (BIOS calls + graphics modes) |
| 2 | Tetris (1:1 C port of the assembly version) |
| 3 | Preemptive multitasking (PIT IRQ0 time slices) |
| 4 | Super Mario (side-scrolling platformer + camera) |
| 5 | Terminus fonts + 3 sizes (pure VGA hardware programming) |
| 6 | In-memory filesystem + 9 file commands |
| 7 | nano-style built-in editor |
| 8 | c4 mini C compiler (the write-C → compile → run loop) |
| 9 | Boot screen (a 32-bit port & polish of a hand-written hello.asm) |
| 10 | Ring 3 user mode (TSS + page-table U/S protection + int 0x80 syscalls + exception handling) |

Detailed development notes, pitfalls and fixes for every milestone live in the `note/` directory (in Chinese).

## 📄 Credits

- **c4** (C in four functions) compiler: originally by Robert Swierczek (public domain); ported here into the kernel
- **Terminus Font**: the classic terminal bitmap font by Dimitar Zhekov
- Early learning plans and notes: `LEARNING_PLAN.md` / `ROADMAP.md` / `docs/` in this repository

> This is a personal learning project; code readability for teaching comes first. Feedback and discussion welcome.
