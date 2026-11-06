#include "vm.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"
#include "string.h"

#define PAGE_SIZE 4096u

static uint32_t align_down(uint32_t value) {
    return value & ~(PAGE_SIZE - 1u);
}

static uint32_t align_up(uint32_t value) {
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static int in_range(uint32_t value, uint32_t begin, uint32_t end) {
    return value >= begin && value < end;
}

static void put_hex(uint32_t value) {
    serial_put_hex(value);
}

static int map_zero_page(task_t* t, uint32_t virt) {
    uint32_t phys = alloc_page();
    if (!phys) return -1;

    k_memset((void*)phys, 0, PAGE_SIZE);
    if (paging_map_page(t->cr3, virt, phys,
                        PAGE_WRITE | PAGE_USER) < 0) {
        free_page(phys);
        return -1;
    }
    return 0;
}

int vm_init_image(task_t* t, const uint8_t* image, uint32_t size) {
    if (!t || !image || size > (VM_IMAGE_LIMIT - VM_IMAGE_BASE)) return -1;

    uint32_t pd = paging_create_user_dir();
    if (!pd) return -1;

    t->cr3 = pd;
    t->image_end = VM_IMAGE_BASE + align_up(size);
    t->brk = VM_HEAP_BASE;
    t->mmap_brk = VM_MMAP_BASE;

    for (uint32_t off = 0; off < align_up(size); off += PAGE_SIZE) {
        uint32_t phys = alloc_page();
        if (!phys) {
            vm_destroy(t);
            return -1;
        }
        k_memset((void*)phys, 0, PAGE_SIZE);
        uint32_t remain = size > off ? size - off : 0;
        if (remain > PAGE_SIZE) remain = PAGE_SIZE;
        if (remain) k_memcpy((void*)phys, image + off, remain);
        if (paging_map_page(pd, VM_IMAGE_BASE + off, phys,
                            PAGE_WRITE | PAGE_USER) < 0) {
            free_page(phys);
            vm_destroy(t);
            return -1;
        }
    }

    serial_puts("[VM] image pid=");
    put_hex(t->pid);
    serial_puts(" bytes=");
    put_hex(size);
    serial_puts(" mapped=");
    put_hex(align_up(size) / PAGE_SIZE);
    serial_puts(" pages\n\r");
    return 0;
}

static void destroy_dir(uint32_t pd) {
    if (!pd) return;
    uint32_t* dir = (uint32_t*)pd;

    __asm__ volatile("cli");
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(dir[pdi] & PAGE_PRESENT)) continue;
        uint32_t pt_phys = dir[pdi] & 0xFFFFF000u;
        uint32_t* pt = (uint32_t*)pt_phys;
        for (uint32_t i = 0; i < 1024; ++i) {
            if (pt[i] & PAGE_PRESENT)
                page_put(pt[i] & 0xFFFFF000u);
        }
        free_page(pt_phys);
        dir[pdi] = 0;
    }
    free_page(pd);
    __asm__ volatile("sti");
}

void vm_destroy(task_t* t) {
    if (!t) return;
    uint32_t pd = t->cr3;
    t->cr3 = 0;
    t->brk = 0;
    t->mmap_brk = 0;
    t->image_end = 0;
    destroy_dir(pd);
}

int vm_clone(task_t* parent, task_t* child) {
    uint32_t child_pd = paging_create_user_dir();
    if (!child_pd) return -1;

    child->cr3 = child_pd;
    child->brk = parent->brk;
    child->mmap_brk = parent->mmap_brk;
    child->image_end = parent->image_end;

    uint32_t* parent_dir = (uint32_t*)parent->cr3;
    uint32_t* child_dir = (uint32_t*)child_pd;

    __asm__ volatile("cli");
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(parent_dir[pdi] & PAGE_PRESENT)) continue;

        uint32_t child_pt_phys = alloc_page();
        if (!child_pt_phys) {
            __asm__ volatile("sti");
            vm_destroy(child);
            return -1;
        }
        uint32_t* parent_pt =
            (uint32_t*)(parent_dir[pdi] & 0xFFFFF000u);
        uint32_t* child_pt = (uint32_t*)child_pt_phys;
        k_memset(child_pt, 0, PAGE_SIZE);

        for (uint32_t i = 0; i < 1024; ++i) {
            if (!(parent_pt[i] & PAGE_PRESENT)) continue;
            uint32_t phys = parent_pt[i] & 0xFFFFF000u;
            page_get(phys);
            child_pt[i] = (parent_pt[i] & ~PAGE_WRITE) | PAGE_COW;
        }
        child_dir[pdi] = child_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    // 只有子页表完整建立后才冻结父页，失败路径无需回滚父 PTE。
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(parent_dir[pdi] & PAGE_PRESENT)) continue;
        uint32_t* parent_pt =
            (uint32_t*)(parent_dir[pdi] & 0xFFFFF000u);
        for (uint32_t i = 0; i < 1024; ++i) {
            if (parent_pt[i] & PAGE_PRESENT)
                parent_pt[i] = (parent_pt[i] & ~PAGE_WRITE) | PAGE_COW;
        }
    }
    paging_flush_all();
    __asm__ volatile("sti");

    serial_puts("[VM] fork pid=");
    put_hex(parent->pid);
    serial_puts(" child=");
    put_hex(child->pid);
    serial_puts(" shared_pages=");
    put_hex(get_shared_pages());
    serial_puts(" (COW)\n\r");
    return 0;
}

static int cowed_page(task_t* t, uint32_t addr, uint32_t error) {
    if (!(error & 2)) return 0;
    uint32_t* pte = paging_user_pte(t->cr3, addr);
    if (!pte || !(*pte & PAGE_PRESENT) || !(*pte & PAGE_COW)) return 0;

    uint32_t old_phys = *pte & 0xFFFFF000u;
    if (page_refcount(old_phys) > 1) {
        uint32_t new_phys = alloc_page();
        if (!new_phys) return 0;
        k_memcpy((void*)new_phys, (void*)old_phys, PAGE_SIZE);
        *pte = new_phys | ((*pte) & 0xFFFu) | PAGE_WRITE;
        *pte &= ~PAGE_COW;
        page_put(old_phys);
        serial_puts("[VM] COW pid=");
        put_hex(t->pid);
        serial_puts(" old=");
        put_hex(old_phys);
        serial_puts(" new=");
        put_hex(new_phys);
        serial_puts("\n\r");
    } else {
        // 对方已经退出，最后一个引用无需复制，恢复可写即可。
        *pte = (*pte | PAGE_WRITE) & ~PAGE_COW;
        serial_puts("[VM] COW pid=");
        put_hex(t->pid);
        serial_puts(" reuse=unique\n\r");
    }
    paging_invalidate_page(align_down(addr));
    return 1;
}

int vm_handle_page_fault(task_t* t, uint32_t addr, uint32_t error) {
    if (!t || !t->is_user || !(error & 4)) return 0;

    if (error & PAGE_PRESENT)
        return cowed_page(t, addr, error);

    if (!in_range(addr, USER_BASE, VM_STACK_TOP)) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" outside user space\n\r");
        return 0;
    }
    if (in_range(addr, VM_STACK_GUARD, VM_STACK_BOTTOM)) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" stack guard page\n\r");
        return 0;
    }

    int allowed =
        in_range(addr, VM_HEAP_BASE, align_up(t->brk)) ||
        in_range(addr, VM_MMAP_BASE, align_up(t->mmap_brk)) ||
        in_range(addr, VM_STACK_BOTTOM, VM_STACK_TOP);
    if (!allowed) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" outside heap/mmap/stack\n\r");
        return 0;
    }

    if (map_zero_page(t, align_down(addr)) < 0) {
        serial_puts("[VM] demand alloc failed pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts("\n\r");
        return 0;
    }

    const char* area = addr < VM_HEAP_LIMIT ? "heap" :
                       addr < VM_MMAP_LIMIT ? "mmap" : "stack";
    serial_puts("[VM] demand pid=");
    put_hex(t->pid);
    serial_puts(" area=");
    serial_puts(area);
    serial_puts(" addr=");
    put_hex(align_down(addr));
    serial_puts("\n\r");
    return 1;
}

uint32_t vm_brk(task_t* t, uint32_t new_break) {
    if (!t || !t->is_user || new_break < VM_HEAP_BASE ||
        new_break > VM_HEAP_LIMIT)
        return 0xFFFFFFFFu;

    uint32_t old_break = t->brk;
    __asm__ volatile("cli");
    if (new_break < old_break) {
        for (uint32_t addr = align_up(new_break);
             addr < align_up(old_break); addr += PAGE_SIZE) {
            uint32_t phys = paging_translate(t->cr3, addr);
            if (phys) {
                paging_unmap_page(t->cr3, addr);
                page_put(align_down(phys));
                paging_invalidate_page(addr);
            }
        }
    }
    t->brk = new_break;
    __asm__ volatile("sti");
    return old_break;
}

uint32_t vm_mmap(task_t* t, uint32_t length) {
    if (!t || !t->is_user || length == 0) return 0xFFFFFFFFu;
    uint32_t bytes = align_up(length);
    if (bytes < length || bytes > VM_MMAP_LIMIT - t->mmap_brk)
        return 0xFFFFFFFFu;

    uint32_t start = t->mmap_brk;
    t->mmap_brk += bytes;
    return start;
}

int vm_user_range_ok(task_t* t, uint32_t addr, uint32_t length) {
    if (!t || !t->is_user || addr < USER_BASE || addr >= VM_STACK_TOP)
        return 0;
    if (length == 0) return 1;
    if (length > VM_STACK_TOP - addr) return 0;

    uint32_t last = addr + length - 1u;
    for (uint32_t page = align_down(addr);
         page <= align_down(last); page += PAGE_SIZE) {
        if (!paging_translate(t->cr3, page)) return 0;
    }
    return 1;
}
