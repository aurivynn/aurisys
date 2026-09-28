// the boot gdt is just null + kernel code/data

#include "arch/gdt.h"

#include "lib/mem.h"

#include <stddef.h>
#include <stdint.h>

namespace {

// i386 tss layout, the cpu reads esp0/ss0 when a ring3 trap fires. it also writes the interrupted context into the tss,
// so the fields past ebx have to sit exactly where the cpu expects them or the old cs and ss end up in the wrong place
struct Tss {
	uint32_t link;
	uint32_t esp0, ss0, esp1, ss1, esp2, ss2;
	uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp;
	uint32_t es, cs, ss, ds, fs, gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap;
};

static_assert(offsetof(Tss, esp) == 56, "the cpu writes the old esp here");
static_assert(offsetof(Tss, ebp) == 60, "the cpu writes the old ebp here");
static_assert(offsetof(Tss, es) == 64, "the cpu writes the old es here");
static_assert(offsetof(Tss, cs) == 68, "the cpu writes the old cs here");
static_assert(offsetof(Tss, ss) == 72, "the cpu writes the old ss here");
static_assert(sizeof(Tss) == 96, "a 32 bit tss with no iomap is 96 bytes");

Tss g_tss __attribute__((section(".paging"))) = {};

// the ring0 stack traps run on, esp0 points at the top
uint8_t g_sys_stack[16384] __attribute__((section(".paging"), aligned(16))) = {};

struct Saw {
	uint16_t limit_lo;
	uint16_t base_lo;
	uint8_t base_mid;
	uint8_t access;
	uint8_t gran; // limit hi nibble + db + g
	uint8_t base_hi;
};

// flat 4g 32bit segment, dpl + code/data from the args
Saw flat_sel(int dpl, bool code) {
	Saw s = {};
	s.limit_lo = 0xFFFF;
	s.gran = 0xCF; // g=1 db=1 so the limit means 4g in pages
	s.access = (code ? 0x9A : 0x92) | (uint8_t)(dpl << 5);
	return s;
}

Saw tss_sel(const Tss& t) {
	Saw s = {};
	const uint32_t base = (uint32_t)(uintptr_t)&t;
	s.limit_lo = (uint16_t)(sizeof(Tss) - 1);
	s.base_lo = (uint16_t)base;
	s.base_mid = (uint8_t)(base >> 16);
	s.access = 0x89; // present, 32bit tss, dpl 0
	s.base_hi = (uint8_t)(base >> 24);
	return s;
}

Saw g_gdt[6] __attribute__((section(".paging"))) = {};

} // namespace

void gdt_init() {
	// .paging is not cleared at boot so zero the tss
	// esp0 is the key field
	memset(&g_tss, 0, sizeof g_tss);
	g_tss.ss0 = kSelKernData;
	g_tss.esp0 = (uint32_t)(uintptr_t)(g_sys_stack + sizeof g_sys_stack);
	// iomap stays 0

	g_gdt[0] = {};				   // null
	g_gdt[1] = flat_sel(0, true);  // kernel code 0x08
	g_gdt[2] = flat_sel(0, false); // kernel data 0x10
	g_gdt[3] = flat_sel(3, true);  // user code 0x1b
	g_gdt[4] = flat_sel(3, false); // user data 0x23
	g_gdt[5] = tss_sel(g_tss);	   // tss 0x28

	struct {
		uint16_t limit;
		uint32_t base;
	} __attribute__((packed)) desc = {(uint16_t)(sizeof(g_gdt) - 1), (uint32_t)(uintptr_t)g_gdt};

	asm volatile("lgdt %0" : : "m"(desc) : "memory");
	asm volatile("jmp $0x08, $1f\n1:" ::: "memory");
	asm volatile("mov %%ax, %%ds\n\tmov %%ax, %%es\n\tmov %%ax, %%fs\n\tmov %%ax, %%gs" : : "a"(kSelKernData));
	asm volatile("ltr %%ax" : : "a"(kSelTss));
}

void gdt_set_kernel_stack(uint32_t esp0) {
	g_tss.esp0 = esp0;
	g_tss.ss0 = kSelKernData;
}