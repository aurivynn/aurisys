#pragma once

#include <stdint.h>

// 2m paging for the kernel, 4k pages for user space
namespace paging {

// one address space. cr3 points at pdpt
struct space {
	uint64_t pdpt[4];  // pdpt[0..3] point at pd[0..3]
	uint64_t* pd[4];   // 512 2m pages each
	uint64_t* pt_app;  // 4k pages for the user arena at 0x40000000
	uint32_t refcount; // shared until later
};

// the boot space
extern space g_boot;

void paging_init();											 // identity map build
void map_range_2M(uint32_t va, uint32_t pa, uint32_t bytes); // va/pa

space* space_create();		// fresh copy of the kernel map
void space_destroy(space*); // drops the tables, frames are bump so they leak
void space_switch(space*);	// cr3 + tlb flush
space* space_current();
uint32_t space_cr3(const space*); // cr3 as the cpu sees it, low bits masked

// 4k page mapping inside one space. the kernel range is 2m mapped so this only works for the user half, the kernel
// keeps its big pages
bool map_page(space* s, uint32_t va, uint32_t frame, bool user);
bool unmap_page(space* s, uint32_t va);
uint32_t page_frame(space* s, uint32_t va); // 0 if not mapped or not 4k

} // namespace paging