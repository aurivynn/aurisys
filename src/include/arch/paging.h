#pragma once

#include <stdint.h>

// 2mb paging
namespace paging {

extern uint64_t g_pdpt[4];	  // cr3 points here
extern uint64_t g_pd[4][512]; // 4 page dirs, 512 2MB pages each

void paging_init();											 // identity map build
void map_range_2M(uint32_t va, uint32_t pa, uint32_t bytes); // va/pa

// 4k pages for the app arena
extern uint64_t g_pt_app[512];
void map_app(uint32_t va, uint32_t pa); // map one 4k frame into the arena
void app_unmap();						// drop every mapping
void app_flush();						// full tlb flush

} // namespace paging