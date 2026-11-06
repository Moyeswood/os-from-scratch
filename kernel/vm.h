#ifndef VM_H
#define VM_H

#include "paging.h"
#include "task.h"
#include "types.h"

// 里程碑 13: 每个用户进程的虚拟地址布局 (32MB..36MB)。
// 只分配实际触及的物理页；中间的空洞不会占用内存。
#define VM_IMAGE_BASE   USER_BASE
#define VM_IMAGE_LIMIT  (USER_BASE + 0x00040000u)
#define VM_HEAP_BASE    VM_IMAGE_LIMIT
#define VM_HEAP_LIMIT   (USER_BASE + 0x000C0000u)
#define VM_MMAP_BASE    VM_HEAP_LIMIT
#define VM_MMAP_LIMIT   (USER_BASE + 0x002F0000u)
#define VM_STACK_GUARD  (USER_BASE + 0x002F0000u)
#define VM_STACK_BOTTOM (USER_BASE + 0x002F1000u)
#define VM_STACK_TOP    (USER_BASE + USER_SIZE)

// 创建独立页目录并装入 flat 镜像。成功后填写 task 的 cr3/brk/mmap_brk。
int vm_init_image(task_t* t, const uint8_t* image, uint32_t size);

// 自动识别 flat 或 ELF32，装入程序并返回用户入口地址。
int vm_init_program(task_t* t, const uint8_t* image, uint32_t size,
                    uint32_t* entry);

// 在新地址空间顶部构造 cdecl 初始栈:
//   [ESP]=fake return, +4=argc, +8=argv, +12=envp
int vm_build_initial_stack(task_t* t, int argc, const char* const* argv,
                           int envc, const char* const* envp,
                           uint32_t* initial_esp);

// fork: 复制页表，用户物理页先共享并标记 COW。
int vm_clone(task_t* parent, task_t* child);

// 释放一个地址空间的所有用户页、页表和页目录。调用时该 CR3 不能在使用中。
void vm_destroy(task_t* t);

// 用户态 #PF 的可恢复路径。返回 1 表示已补页/复制并应重试原指令。
int vm_handle_page_fault(task_t* t, uint32_t addr, uint32_t error);

// brk 接收新断点，返回旧断点；失败返回 0xFFFFFFFF。
uint32_t vm_brk(task_t* t, uint32_t new_break);

// 从专用 mmap 窗口预留一段地址，物理页在首次访问时才分配。
uint32_t vm_mmap(task_t* t, uint32_t length);

// 系统调用读用户指针前的逐页映射检查，防止内核对未映射页取数。
int vm_user_range_ok(task_t* t, uint32_t addr, uint32_t length);

// 内核写用户内存。与普通 memcpy 不同，遇到 COW 页时先私有化，
// 避免 Ring 0 写只读 PTE 时绕过 fork 隔离。
int vm_write_user(task_t* t, uint32_t addr, const void* source,
                  uint32_t length);

#endif
