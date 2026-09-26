#include "lib/heap.h"

#include <stdint.h>

extern "C" char __kernel_end;

namespace {

uint32_t g_start = 0;
uint32_t g_brk = 0;
uint32_t g_end = 0; // end of the usable region that holds the image

} // namespace

void heap_init(const bootinfo* bi) {
	g_start = (g_brk = ((uint32_t)&__kernel_end + 7u) & ~7u);
	g_end = 0;
	// find the usable region that contains the kernel
	for (uint32_t i = 0; i < bi->mem_entries && i < 32; ++i) {
		const e820_entry* e = &bi->mem[i];
		if (e->type != 1)
			continue;
		const uint32_t base = (uint32_t)e->base;
		const uint32_t end = base + (uint32_t)e->len;
		if (base <= 0x100000u && end > 0x100000u && end > g_end)
			g_end = end;
	}
	if (g_end <= g_start)
		g_end = g_start + 0x10000; // nothing sane
}

void* kmalloc(size_t size) {
	if (size == 0)
		size = 1;
	size = (size + 7u) & ~7u;
	if (g_brk + size > g_end)
		return nullptr;
	void* p = (void*)g_brk;
	g_brk += (uint32_t)size;
	return p;
}

void* kframe_alloc() {
	void* p = kmalloc(4096u + 4095u);
	if (!p)
		return nullptr;
	return (void*)(((uint32_t)p + 4095u) & ~4095u);
}

void kfree(void*) {
	// bump allocator, no reuse yet
}

void heap_stats(size_t* total, size_t* used, size_t* largest_free) {
	if (total)
		*total = g_end - g_start;
	if (used)
		*used = g_brk - g_start;
	if (largest_free)
		*largest_free = g_end - g_brk;
}