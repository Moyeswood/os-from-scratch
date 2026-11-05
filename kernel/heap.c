#include "heap.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "vga.h"

#define HEAP_MAGIC       0x4B4D414Cu  /* "KMAL" */
#define HEAP_ALIGN       16u
#define HEAP_HEADER_SIZE 24u
#define HEAP_FLAG_FREE   1u

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;       /* payload bytes */
    uint32_t tail_magic;
    uint32_t flags;
    uint32_t pages;
    struct heap_block* next;
} heap_block_t;

typedef char heap_header_size_check[
    sizeof(heap_block_t) == HEAP_HEADER_SIZE ? 1 : -1
];

static heap_block_t* heap_head;
static uint32_t heap_pages;

static uint32_t align_up(uint32_t n) {
    return (n + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

static uint32_t irq_save_disable() {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static void insert_block(heap_block_t* block) {
    if (!heap_head || block < heap_head) {
        block->next = heap_head;
        heap_head = block;
        return;
    }
    heap_block_t* cur = heap_head;
    while (cur->next && cur->next < block) cur = cur->next;
    block->next = cur->next;
    cur->next = block;
}

static void coalesce() {
    heap_block_t* cur = heap_head;
    while (cur && cur->next) {
        uint8_t* cur_end = (uint8_t*)cur + HEAP_HEADER_SIZE + cur->size;
        if ((cur->flags & HEAP_FLAG_FREE) &&
            (cur->next->flags & HEAP_FLAG_FREE) &&
            cur_end == (uint8_t*)cur->next) {
            cur->size += HEAP_HEADER_SIZE + cur->next->size;
            cur->pages += cur->next->pages;
            cur->next = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

static int grow_heap(uint32_t need) {
    uint32_t total = HEAP_HEADER_SIZE + align_up(need);
    uint32_t pages = (total + 4095) / 4096;
    uint32_t addr = alloc_pages(pages);
    if (!addr) {
        serial_puts("[HEAP] grow failed\n\r");
        return 0;
    }
    heap_block_t* block = (heap_block_t*)addr;
    block->magic = HEAP_MAGIC;
    block->size = pages * 4096 - HEAP_HEADER_SIZE;
    block->tail_magic = HEAP_MAGIC;
    block->flags = HEAP_FLAG_FREE;
    block->pages = pages;
    block->next = 0;
    heap_pages += pages;
    insert_block(block);
    coalesce();
    serial_puts("[HEAP] grow pages=");
    serial_put_hex(pages);
    serial_puts(" at=");
    serial_put_hex(addr);
    serial_puts("\n\r");
    return 1;
}

void heap_init() {
    heap_head = 0;
    heap_pages = 0;
    grow_heap(4096);
    serial_puts("[HEAP] initialized\n\r");
}

void* kmalloc(uint32_t size) {
    if (!size) return 0;
    uint32_t need = align_up(size);
    uint32_t flags = irq_save_disable();

    heap_block_t* block = heap_head;
    while (block) {
        if ((block->flags & HEAP_FLAG_FREE) && block->size >= need) break;
        block = block->next;
    }
    if (!block) {
        if (!grow_heap(need)) {
            irq_restore(flags);
            return 0;
        }
        block = heap_head;
        while (block && !((block->flags & HEAP_FLAG_FREE) && block->size >= need))
            block = block->next;
        if (!block) {
            irq_restore(flags);
            return 0;
        }
    }

    if (block->size >= need + HEAP_HEADER_SIZE + HEAP_ALIGN) {
        uint8_t* split_addr = (uint8_t*)block + HEAP_HEADER_SIZE + need;
        heap_block_t* split = (heap_block_t*)split_addr;
        split->magic = HEAP_MAGIC;
        split->size = block->size - need - HEAP_HEADER_SIZE;
        split->tail_magic = HEAP_MAGIC;
        split->flags = HEAP_FLAG_FREE;
        split->pages = 0;
        split->next = block->next;
        block->next = split;
        block->size = need;
    }

    block->flags = 0;
    irq_restore(flags);
    return (void*)((uint8_t*)block + HEAP_HEADER_SIZE);
}

void* kzalloc(uint32_t size) {
    void* ptr = kmalloc(size);
    if (ptr) k_memset(ptr, 0, size);
    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    uint32_t flags = irq_save_disable();
    heap_block_t* target = (heap_block_t*)((uint8_t*)ptr - HEAP_HEADER_SIZE);
    heap_block_t* cur = heap_head;
    while (cur && cur != target) cur = cur->next;
    if (!cur || cur->magic != HEAP_MAGIC || cur->tail_magic != HEAP_MAGIC ||
        (cur->flags & HEAP_FLAG_FREE)) {
        serial_puts("[HEAP] invalid free ignored\n\r");
        irq_restore(flags);
        return;
    }
    cur->flags |= HEAP_FLAG_FREE;
    coalesce();
    irq_restore(flags);
}

void* krealloc(void* ptr, uint32_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) {
        kfree(ptr);
        return 0;
    }
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - HEAP_HEADER_SIZE);
    if (block->magic == HEAP_MAGIC && block->size >= size) return ptr;
    void* next = kmalloc(size);
    if (!next) return 0;
    if (block->magic == HEAP_MAGIC) {
        uint32_t copy = block->size < size ? block->size : size;
        k_memcpy(next, ptr, copy);
    }
    kfree(ptr);
    return next;
}

int heap_check() {
    uint32_t flags = irq_save_disable();
    heap_block_t* cur = heap_head;
    while (cur) {
        if (cur->magic != HEAP_MAGIC || cur->tail_magic != HEAP_MAGIC) {
            irq_restore(flags);
            return 0;
        }
        if (cur->next && (uint8_t*)cur + HEAP_HEADER_SIZE + cur->size > (uint8_t*)cur->next) {
            irq_restore(flags);
            return 0;
        }
        cur = cur->next;
    }
    irq_restore(flags);
    return 1;
}

int heap_selftest() {
    char* a = (char*)kmalloc(32);
    char* b = (char*)kmalloc(200);
    char* c = (char*)kmalloc(1000);
    int ok = a && b && c;
    if (ok) {
        for (int i = 0; i < 32; ++i) a[i] = (char)i;
        for (int i = 0; i < 200; ++i) b[i] = (char)(255 - i);
        for (int i = 0; i < 1000; ++i) c[i] = (char)(i * 7);
        a = (char*)krealloc(a, 128);
        ok = a && ok;
        if (a) for (int i = 0; i < 32; ++i)
            if (a[i] != (char)i) ok = 0;
    }
    kfree(a);
    kfree(b);
    kfree(c);
    ok = ok && heap_check();
    serial_puts("[HEAP] selftest ");
    serial_puts(ok ? "PASS\n\r" : "FAIL\n\r");
    return ok;
}

uint32_t heap_total_bytes() {
    uint32_t total = 0;
    for (heap_block_t* b = heap_head; b; b = b->next) total += b->size;
    return total;
}

uint32_t heap_used_bytes() {
    uint32_t used = 0;
    for (heap_block_t* b = heap_head; b; b = b->next)
        if (!(b->flags & HEAP_FLAG_FREE)) used += b->size;
    return used;
}

uint32_t heap_free_bytes() { return heap_total_bytes() - heap_used_bytes(); }

uint32_t heap_block_count() {
    uint32_t n = 0;
    for (heap_block_t* b = heap_head; b; b = b->next) ++n;
    return n;
}

uint32_t heap_page_count() { return heap_pages; }

static void put_dec(uint32_t n) {
    if (!n) { vga_putc('0'); return; }
    char tmp[12];
    int pos = 0;
    while (n) { tmp[pos++] = (char)('0' + n % 10); n /= 10; }
    while (pos) vga_putc(tmp[--pos]);
}

void heap_print_stats() {
    vga_puts("\n\rKernel heap: ");
    put_dec(heap_total_bytes() / 1024); vga_puts(" KB total, ");
    put_dec(heap_used_bytes()); vga_puts(" B used, ");
    put_dec(heap_free_bytes()); vga_puts(" B free\n\r");
    vga_puts("Pages="); put_dec(heap_pages);
    vga_puts(", Blocks="); put_dec(heap_block_count());
    vga_puts(", Check="); vga_puts(heap_check() ? "OK" : "FAIL");
    vga_puts("\n\r");
    serial_puts("[SH] heap: pages="); serial_put_hex(heap_pages);
    serial_puts(" blocks="); serial_put_hex(heap_block_count());
    serial_puts(" used="); serial_put_hex(heap_used_bytes());
    serial_puts(" free="); serial_put_hex(heap_free_bytes());
    serial_puts(" check="); serial_puts(heap_check() ? "OK" : "FAIL");
    serial_puts("\n\r");
}

void heap_print_blocks() {
    vga_puts("\n\rHeap blocks:\n\r");
    for (heap_block_t* b = heap_head; b; b = b->next) {
        vga_puts((b->flags & HEAP_FLAG_FREE) ? "  free  " : "  used  ");
        put_dec(b->size);
        vga_puts(" bytes, pages=");
        put_dec(b->pages);
        vga_puts("\n\r");
        serial_puts((b->flags & HEAP_FLAG_FREE) ? "[SH] heap block free size=" : "[SH] heap block used size=");
        serial_put_hex(b->size);
        serial_puts(" pages=");
        serial_put_hex(b->pages);
        serial_puts("\n\r");
    }
}
