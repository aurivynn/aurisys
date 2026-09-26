#include "arch/paging.h"

#include "lib/heap.h"
#include "lib/mem.h"

#include <stdint.h>

namespace paging {

namespace {

uint64_t g_pd_store[4][512] __attribute__((section(".paging"), aligned(4096)));
uint64_t g_pt_store[512] __attribute__((section(".paging"), aligned(4096)));

space* g_current = nullptr;

void flush() { asm volatile("mov %0, %%cr3" ::"r"((uint32_t)(uintptr_t)g_current->pdpt) : "memory"); }

} // namespace

__attribute__((aligned(4096))) space g_boot;

void map_range_2M(uint32_t va, uint32_t pa, uint32_t bytes) {
	const uint32_t n = (bytes + 0x1FFFFFu) >> 21;
	for (uint32_t i = 0; i < n; ++i) {
		const uint32_t a = va + i * 0x200000u;
		if (a >> 30 > 3)
			break;															  // past 4GiB
		const uint64_t base = (uint64_t)(pa + i * 0x200000u) & 0xFFE00000ull; // fvdewfnjkre
		g_boot.pd[a >> 30][(a >> 21) & 0x1FF] = base | (1ull << 7) | 3;		  // PS | RW | P
	}
}

void paging_init() {
	for (int i = 0; i < 4; ++i) {
		g_boot.pd[i] = g_pd_store[i];
		g_boot.pdpt[i] = ((uint64_t)(uintptr_t)g_pd_store[i] & 0xFFFFF000ull) | 3; // P | RW
	}
	g_boot.pt_app = g_pt_store;

	map_range_2M(0x00000000, 0x00000000, 0x80000000); // low 2GB
	map_range_2M(0x80000000, 0x80000000, 0x80000000); // high 2GB (framebuffer)

	g_boot.pd[1][0] = ((uint64_t)(uintptr_t)g_pt_store & 0xFFFFF000ull) | 7; // P | RW | U
	memset(g_pt_store, 0, sizeof g_pt_store);

	uint32_t cr4;
	asm volatile("mov %%cr4, %0" : "=r"(cr4) : : "memory");
	cr4 |= (1u << 5) | (1u << 4); // PSE | PAE
	asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");

	g_current = &g_boot;
	flush();

	uint32_t cr0;
	asm volatile("mov %%cr0, %0" : "=r"(cr0) : : "memory");
	cr0 |= (1u << 31); // PG is bit 31, bit 0 is PE and already set
	asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
}

// the user half only lives at 0x40000000, one 2m slot worth of it
static bool user_slot(space* s, uint32_t va) { return (va >> 21) == 512 && va < 0x40200000u; }

space* space_create() {
	space* s = (space*)kframe_alloc();
	if (!s)
		return nullptr;
	for (int i = 0; i < 4; ++i) {
		uint64_t* pd = (uint64_t*)kframe_alloc();
		if (!pd)
			return nullptr;
		memcpy(pd, g_boot.pd[i], 4096);
		s->pd[i] = pd;
	}
	s->pt_app = (uint64_t*)kframe_alloc();
	if (!s->pt_app)
		return nullptr;
	memset(s->pt_app, 0, 4096);
	s->pd[1][0] = ((uint64_t)(uintptr_t)s->pt_app & 0xFFFFF000ull) | 7; // P | RW | U
	s->pdpt[0] = ((uint64_t)(uintptr_t)s->pd[0] & 0xFFFFF000ull) | 3;
	s->pdpt[1] = ((uint64_t)(uintptr_t)s->pd[1] & 0xFFFFF000ull) | 3;
	s->pdpt[2] = ((uint64_t)(uintptr_t)s->pd[2] & 0xFFFFF000ull) | 3;
	s->pdpt[3] = ((uint64_t)(uintptr_t)s->pd[3] & 0xFFFFF000ull) | 3;
	s->refcount = 1;
	return s;
}

void space_destroy(space* s) {
	// frames come from the bump allocator so they are not handed back yet
	(void)s;
}

void space_switch(space* s) {
	if (!s)
		return;
	g_current = s;
	flush();
}

space* space_current() { return g_current; }

uint32_t space_cr3(const space* s) { return (uint32_t)(uintptr_t)s->pdpt & ~0xFFFu; }

bool map_page(space* s, uint32_t va, uint32_t frame, bool user) {
	if (!s || !user_slot(s, va))
		return false;
	const uint32_t i = (va >> 12) & 0x1FF;
	if ((s->pt_app[i] & 1) && (s->pt_app[i] & 0xFFFFF000u) == (frame & 0xFFFFF000u))
		return true;												   // already there
	s->pt_app[i] = (uint64_t)(frame & 0xFFFFF000u) | (user ? 7u : 3u); // u = user
	asm volatile("invlpg (%0)" ::"r"(va & ~0xFFFu) : "memory");
	return true;
}

bool unmap_page(space* s, uint32_t va) {
	if (!s || !user_slot(s, va))
		return false;
	const uint32_t i = (va >> 12) & 0x1FF;
	if (!(s->pt_app[i] & 1))
		return false;
	s->pt_app[i] = 0;
	asm volatile("invlpg (%0)" ::"r"(va & ~0xFFFu) : "memory");
	return true;
}

uint32_t page_frame(space* s, uint32_t va) {
	if (!s || !user_slot(s, va))
		return 0;
	const uint64_t e = s->pt_app[(va >> 12) & 0x1FF];
	if (!(e & 1) || (e & 0x80))
		return 0; // not present or still a 2m page
	return (uint32_t)(e & 0xFFFFF000u);
}

} // namespace paging
