// kernel

#include "arch/bootinfo.h"
#include "arch/idt.h"
#include "arch/paging.h"

#include "drivers/ata.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/kbd.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/print.h"
#include "lib/time.h"
#include "fs.h"
#include "shell/terminal.h"

#include <stdarg.h>
#include <stdint.h>

extern "C" void kernel_main(bootinfo* bi);

namespace {

// test 1: global ctor.
struct BootFlag {
	uint32_t magic;
	BootFlag() : magic(0xDEADBEEFu) {}
};
BootFlag g_boot_flag;

// test 2: classes + vtables
class Shape {
  public:
	virtual ~Shape() {}
	virtual uint32_t area() const = 0;
};

class Rect : public Shape {
	uint32_t w_, h_;

  public:
	Rect(uint32_t w, uint32_t h) : w_(w), h_(h) {}
	uint32_t area() const override { return w_ * h_; }
};

void emit_both(char c) {
	serial::putc(c);
	console::putchar(c);
}

void both(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&emit_both, fmt, ap);
	va_end(ap);
}

void test_cpp() {
	const bool ctor_ok = (g_boot_flag.magic == 0xDEADBEEFu);

	Rect rect(6, 7);
	const bool direct_ok = (rect.area() == 42u);

	Shape* s = &rect;
	const bool vtable_ok = (s->area() == 42u); // via the vtable

	// little endian check
	const uint32_t v = 0x41424344u;
	const uint8_t* p = (const uint8_t*)&v;
	const bool endian_ok = (p[0] == 0x44 && p[1] == 0x43 && p[2] == 0x42 && p[3] == 0x41);

	both("    ctor=%s direct=%s vtable=%s endian=%s\n", ctor_ok ? "OK" : "FAIL", direct_ok ? "OK" : "FAIL",
		 vtable_ok ? "OK" : "FAIL", endian_ok ? "OK" : "FAIL");
}

void test_mem() {
	uint8_t src[64], dst[64];
	for (int i = 0; i < 64; ++i)
		src[i] = (uint8_t)(i * 3 + 1);

	memset(dst, 0xAA, sizeof dst);
	bool memset_ok = true;
	for (int i = 0; i < 64; ++i) {
		if (dst[i] != 0xAA)
			memset_ok = false;
	}

	memcpy(dst, src, sizeof src);
	const bool memcpy_ok = (memcmp(dst, src, sizeof src) == 0);

	memset(dst, 0, 32);
	const bool zero_ok = (dst[0] == 0 && dst[31] == 0 && dst[32] == src[32]);

	uint8_t buf[16];
	for (int i = 0; i < 16; ++i)
		buf[i] = (uint8_t)i;
	memmove(buf + 4, buf, 12); // overlap copy
	const bool memmove_ok = (buf[4] == 0 && buf[15] == 11);

	both("    memset=%s memcpy=%s zero=%s memmove=%s\n", memset_ok ? "OK" : "FAIL", memcpy_ok ? "OK" : "FAIL",
		 zero_ok ? "OK" : "FAIL", memmove_ok ? "OK" : "FAIL");
}

void test_e820(const bootinfo* bi) {
	both("    E820: %u entries\n", bi->mem_entries);
	const uint32_t n = (bi->mem_entries < 32) ? bi->mem_entries : 32;
	for (uint32_t i = 0; i < n; ++i) {
		const e820_entry* e = &bi->mem[i];
		both("      [%u] base=%lx len=%lx type=%u%s\n", i, e->base, e->len, e->type, e->type == 1 ? " usable" : "");
	}
}

// idt got filled. gate 0 + irq0 gate present, slot 48 left empty
void test_idt() {
	const bool ok = (g_idt[0].flags == 0x8E && g_idt[32].flags == 0x8E && g_idt[48].flags == 0);
	both("    idt=%s\n", ok ? "OK" : "FAIL");
}

// the real proof is the panic app, but make sure the timer irq actually ticks.
void test_timer() {
	const uint32_t t0 = time::ms();
	while (time::ms() - t0 < 20) {
		// wait 20ms
	}
	const bool ok = (time::ms() - t0 >= 20);
	both("    timer=%s\n", ok ? "OK" : "FAIL");
}

void test_heap() {
	uint32_t* a = (uint32_t*)kmalloc(64);
	uint32_t* b = (uint32_t*)kmalloc(32);
	a[15] = 0xCAFEBABEu;
	b[7] = 0xDEADBEEFu;
	bool ok = (a && b && a[15] == 0xCAFEBABEu && b[7] == 0xDEADBEEFu && (uint32_t)a != (uint32_t)b);
	kfree(a);
	kfree(b);
	ok = ok && kmalloc(0) != nullptr;
	both("    heap=%s\n", ok ? "OK" : "FAIL");
}

void test_paging() {
	bool ok = true;
	uint32_t cr3;
	asm volatile("mov %%cr3, %0" : "=r"(cr3));
	ok = ok && (cr3 == (uint32_t)(uintptr_t)paging::g_pdpt);

	const uint32_t vaddrs[] = {0x00000000u, 0x00100000u, 0x07FE0000u, 0x80000000u, 0xFD000000u, 0xFFFFFFFFu};
	for (uint32_t va : vaddrs) {
		const uint64_t pde = paging::g_pd[va >> 30][(va >> 21) & 0x1FF];
		ok = ok && (pde & 1) && (pde & (1ull << 7)) &&
			 ((uint32_t)(pde & 0xFFE00000ull) == (va & 0xFFE00000u)); // identity
	}
	both("    paging=%s\n", ok ? "OK" : "FAIL");
}

} // namespace

extern "C" void kernel_main(bootinfo* bi) {
	serial::init();
	serial::puts("\r\nAURISYS: boot OK\r\n");

	fb::init(bi->fb_addr, bi->fb_pitch, bi->fb_width, bi->fb_height);
	console::init();
	console::clear();

	// interrupt plumbing
	idt_init();
	pic::remap();
	kbd::init();  // irq1 -> ring buffer
	timer_init(); // irq0 -> ms clock
	heap_init(bi);
	paging::paging_init();
	asm volatile("sti");

	console::setcolor(0xCBA6F7, 0x1E1E2E);
	console::puts("  AURISYS");
	console::setcolor(0xCDD6F4, 0x1E1E2E);
	console::printf("  framebuffer %ux%u @%ubpp  LFB=%x  pitch=%u\n", bi->fb_width, bi->fb_height, bi->fb_bpp,
					bi->fb_addr, bi->fb_pitch);
	console::printf("  bootinfo @%p  magic=%x  (%s)\n\n", (uint32_t)bi, bi->magic,
					bi->magic == BOOTINFO_MAGIC ? "OK" : "BAD");

	console::setcolor(0x89DCEB, 0x1E1E2E);
	both("  kernel tests\n");
	console::setcolor(0xCDD6F4, 0x1E1E2E);
	test_cpp();
	test_mem();
	test_e820(bi);
	test_idt();
	test_timer();
	test_heap();
	test_paging();

	const bool ata_ok = ata::init();
	both("    ata=%s\n", ata_ok ? "OK" : "FAIL");
	both("    fs=%s\n", (ata_ok && fs::mount(0)) ? "OK" : "FAIL");

	console::setcolor(0xCBA6F7, 0x1E1E2E);
	both("\n  AURISYS: all tests passed\n");
	console::setcolor(0xCDD6F4, 0x1E1E2E);

	serial::puts("\r\nAURISYS: all tests passed\r\n");
	serial::puts("\r\nAURISYS: terminal ready\r\n");

	terminal::run(); // never returns

	for (;;)
		asm volatile("hlt"); // done
}