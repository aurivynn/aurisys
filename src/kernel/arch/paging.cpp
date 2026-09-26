#include "arch/paging.h"

#include "lib/mem.h"

#include <stdint.h>

namespace paging {

uint64_t g_pdpt[4] __attribute__((section(".paging"), aligned(4096)));
uint64_t g_pd[4][512] __attribute__((section(".paging"), aligned(4096)));
uint64_t g_pt_app[512] __attribute__((section(".paging"), aligned(4096)));

void map_range_2M(uint32_t va, uint32_t pa, uint32_t bytes) {
	const uint32_t n = (bytes + 0x1FFFFFu) >> 21;
	for (uint32_t i = 0; i < n; ++i) {
		const uint32_t a = va + i * 0x200000u;
		if (a >> 30 > 3)
			break;															  // past 4GiB
		const uint64_t base = (uint64_t)(pa + i * 0x200000u) & 0xFFE00000ull; // fvdewfnjkre
		g_pd[a >> 30][(a >> 21) & 0x1FF] = base | (1ull << 7) | 3;			  // PS | RW | P
	}
}

void map_app(uint32_t va, uint32_t pa) {
	// only the 2m slot at 0x40000000 hosts the arena
	if ((va >> 21) != 512)
		return;
	// that slot used to be a 2m identity page at 1gb, now it is a table
	g_pd[1][0] = ((uint64_t)(uintptr_t)g_pt_app & 0xFFFFF000ull) | 3;
	g_pt_app[(va >> 12) & 0x1FF] = ((uint64_t)(pa & 0xFFFFF000u)) | 3;
	asm volatile("invlpg (%0)" ::"r"(va & ~0xFFFu) : "memory");
}

void app_unmap() {
	memset(g_pt_app, 0, sizeof g_pt_app);
	app_flush();
}

void app_flush() { asm volatile("mov %0, %%cr3" ::"r"((uint32_t)(uintptr_t)g_pdpt) : "memory"); }

void paging_init() {
	map_range_2M(0x00000000, 0x00000000, 0x80000000); // low 2GB
	map_range_2M(0x80000000, 0x80000000, 0x80000000); // high 2GB (framebuffer)

	for (int i = 0; i < 4; ++i)
		g_pdpt[i] = ((uint64_t)(uintptr_t)g_pd[i] & 0xFFFFF000ull) | 3; // P | RW

	uint32_t cr4;
	asm volatile("mov %%cr4, %0" : "=r"(cr4) : : "memory");
	cr4 |= (1u << 5) | (1u << 4); // PSE | PAE
	asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");

	asm volatile("mov %0, %%cr3" ::"r"((uint32_t)(uintptr_t)g_pdpt) : "memory");
	asm volatile("invlpg (%0)" ::"r"(0u) : "memory"); // clear whatever is in the tlb

	uint32_t cr0;
	asm volatile("mov %%cr0, %0" : "=r"(cr0) : : "memory");
	cr0 |= 0x80000000u; // PG
	asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
}

} // namespace paging