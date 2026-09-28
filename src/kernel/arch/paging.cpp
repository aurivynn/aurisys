#include "arch/paging.h"

#include "lib/heap.h"
#include "task.h"
#include "lib/mem.h"

#include <stdint.h>

namespace paging {

namespace {

uint64_t g_pd_store[4][512] __attribute__((section(".paging"), aligned(4096)));
uint64_t g_pt_store[512] __attribute__((section(".paging"), aligned(4096)));
uint64_t g_pt_heap[512] __attribute__((section(".paging"), aligned(4096)));

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
	g_boot.pt_code = g_pt_store;
	g_boot.pt_heap = g_pt_heap;

	map_range_2M(0x00000000, 0x00000000, 0x80000000); // low 2GB
	map_range_2M(0x80000000, 0x80000000, 0x80000000); // high 2GB (framebuffer)

	g_boot.pd[1][0] = ((uint64_t)(uintptr_t)g_pt_store & 0xFFFFF000ull) | 7; // P | RW | U
	g_boot.pd[1][1] = ((uint64_t)(uintptr_t)g_pt_heap & 0xFFFFF000ull) | 7;	 // P | RW | U
	memset(g_pt_store, 0, sizeof g_pt_store);
	memset(g_pt_heap, 0, sizeof g_pt_heap);

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

// the user half is two 2m slots worth of 4k pages
static uint64_t** user_table(space* s, uint32_t va) {
	if (va >= 0x40000000u && va < 0x40200000u)
		return &s->pt_code;
	if (va >= 0x40200000u && va < 0x40400000u)
		return &s->pt_heap;
	return nullptr;
}

static void link_tables(space* s) {
	s->pd[1][0] = ((uint64_t)(uintptr_t)s->pt_code & 0xFFFFF000ull) | 7; // P | RW | U
	s->pd[1][1] = ((uint64_t)(uintptr_t)s->pt_heap & 0xFFFFF000ull) | 7;

	if (g_current == s)
		asm volatile("mov %0, %%cr3" ::"r"((uint32_t)(uintptr_t)s->pdpt) : "memory");
}

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
	s->pt_code = (uint64_t*)kframe_alloc();
	s->pt_heap = (uint64_t*)kframe_alloc();
	if (!s->pt_code || !s->pt_heap)
		return nullptr;
	memset(s->pt_code, 0, 4096);
	memset(s->pt_heap, 0, 4096);
	s->pdpt[0] = ((uint64_t)(uintptr_t)s->pd[0] & 0xFFFFF000ull) | 3;
	s->pdpt[1] = ((uint64_t)(uintptr_t)s->pd[1] & 0xFFFFF000ull) | 3;
	s->pdpt[2] = ((uint64_t)(uintptr_t)s->pd[2] & 0xFFFFF000ull) | 3;
	s->pdpt[3] = ((uint64_t)(uintptr_t)s->pd[3] & 0xFFFFF000ull) | 3;
	link_tables(s);
	s->refcount = 1;
	s->cow = nullptr;
	return s;
}

void space_destroy(space* s) {
	if (!s || s == &g_boot)
		return;

	if (s->cow) {
		cow_group* g = s->cow;
		if (s->pt_code != g->pt_code) {
			kfree(s->pt_code);
			kfree(s->pt_heap);
		}
		s->cow = nullptr;
		if (--g->refs == 0) {
			kfree(g->pt_code);
			kfree(g->pt_heap);
			kfree(g);
		}
		s->pt_code = nullptr;
		s->pt_heap = nullptr;
	} else {
		kfree(s->pt_code);
		kfree(s->pt_heap);
		s->pt_code = nullptr;
		s->pt_heap = nullptr;
	}
	for (int i = 0; i < 4; ++i) {
		kfree(s->pd[i]);
		s->pd[i] = nullptr;
	}
	kfree(s);
}

static bool user_va(space* s, uint32_t va) { return user_table(s, va) != nullptr; }

bool space_share_user(space* child, space* parent) {
	if (!child || !parent || child == parent || !parent->pt_code || !parent->pt_heap)
		return false;

	cow_group* g = parent->cow;
	if (g && parent->pt_code != g->pt_code) {
		if (--g->refs == 0) {
			kfree(g->pt_code);
			kfree(g->pt_heap);
			kfree(g);
		}
		g = nullptr;
		parent->cow = nullptr;
	}

	if (!g) {
		g = (cow_group*)kframe_alloc();
		if (!g)
			return false;
		g->pt_code = parent->pt_code;
		g->pt_heap = parent->pt_heap;
		g->refs = 1; // the parent which now has to unshare like anybody else
		parent->cow = g;
	}

	for (int i = 0; i < 512; ++i) {
		if (g->pt_code[i] & 1)
			g->pt_code[i] &= ~2ull;
		if (g->pt_heap[i] & 1)
			g->pt_heap[i] &= ~2ull;
	}
	link_tables(parent);

	kfree(child->pt_code);
	kfree(child->pt_heap);
	child->pt_code = g->pt_code;
	child->pt_heap = g->pt_heap;
	child->cow = g;

	link_tables(child);
	g->refs++;
	return true;
}
static bool unshare(space* s) {
	cow_group* g = s->cow;
	if (!g)
		return true; // not sharing, the tables are already this spaces own
	if (s->pt_code != g->pt_code)
		return true; // already out of the group from an earlier unshare

	uint64_t* code = (uint64_t*)kframe_alloc();
	uint64_t* heap = (uint64_t*)kframe_alloc();
	if (!code || !heap) {
		kfree(code);
		kfree(heap);
		return false;
	}
	memcpy(code, g->pt_code, 4096);
	memcpy(heap, g->pt_heap, 4096);
	s->pt_code = code;
	s->pt_heap = heap;
	link_tables(s);
	return true;
}

bool space_cow_fault(space* s, uint32_t addr) {
	if (!s || !s->cow || !user_va(s, addr))
		return false;

	const uint32_t page = addr & ~0xFFFu;
	uint64_t** tbl = user_table(s, page);
	if (!tbl)
		return false;
	uint64_t* pt = *tbl;
	const uint32_t i = (page >> 12) & 0x1FF;
	const uint64_t ent = pt[i];

	if (!(ent & 1) || !(ent & 4) || (ent & 2))
		return false;

	const uint32_t src = (uint32_t)(ent & 0xFFFFF000u);

	void* nf = kframe_alloc();
	if (!nf)
		return false;
	memcpy(nf, (void*)(uintptr_t)src, 4096);

	if (!unshare(s)) {
		kframe_free(nf);
		return false;
	}

	map_page(s, page, (uint32_t)(uintptr_t)nf, true);
	return true;
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
	uint64_t** tbl = s ? user_table(s, va) : nullptr;
	if (!tbl)
		return false;
	uint64_t* pt = *tbl;
	const uint32_t i = (va >> 12) & 0x1FF;
	if ((pt[i] & 1) && (pt[i] & 0xFFFFF000u) == (frame & 0xFFFFF000u))
		return true;											// already there
	pt[i] = (uint64_t)(frame & 0xFFFFF000u) | (user ? 7u : 3u); // u = user
	asm volatile("invlpg (%0)" ::"r"(va & ~0xFFFu) : "memory");
	return true;
}

bool unmap_page(space* s, uint32_t va) {
	uint64_t** tbl = s ? user_table(s, va) : nullptr;
	if (!tbl)
		return false;
	uint64_t* pt = *tbl;
	const uint32_t i = (va >> 12) & 0x1FF;
	if (!(pt[i] & 1))
		return false;
	pt[i] = 0;
	asm volatile("invlpg (%0)" ::"r"(va & ~0xFFFu) : "memory");
	return true;
}

uint32_t page_frame(space* s, uint32_t va) {
	uint64_t** tbl = s ? user_table(s, va) : nullptr;
	if (!tbl)
		return 0;
	const uint64_t e = (*tbl)[(va >> 12) & 0x1FF];
	if (!(e & 1) || (e & 0x80))
		return 0; // not present or still a 2m page
	return (uint32_t)(e & 0xFFFFF000u);
}

} // namespace paging
