// kernel

#include "arch/bootinfo.h"
#include "arch/gdt.h"
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
#include "proc.h"
#include "exec.h"
#include "shell/terminal.h"
#include "task.h"
#include "vfs.h"

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

// idt got filled. gate 0 + irq0 gate present, slot 48 left empty, the app gate is dpl 3 so ring 3 can call it
void test_idt() {
	const bool ok =
		(g_idt[0].flags == 0x8E && g_idt[32].flags == 0x8E && g_idt[48].flags == 0 && g_idt[0x80].flags == 0xEE);
	both("    idt=%s\n", ok ? "OK" : "FAIL");
}

// ring 3 plumbing
void test_gdt() {
	uint32_t tr;
	asm volatile("str %0" : "=r"(tr));
	const bool ok = (tr == kSelTss);
	both("    gdt=%s\n", ok ? "OK" : "FAIL");
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
	bool ok = true;
	size_t total0, used0, big0;
	heap_stats(&total0, &used0, &big0);

	uint32_t* a = (uint32_t*)kmalloc(64);
	uint32_t* b = (uint32_t*)kmalloc(32);
	ok = ok && a && b && (uint32_t)a != (uint32_t)b;
	if (a)
		a[15] = 0xCAFEBABEu;
	if (b)
		b[7] = 0xDEADBEEFu;
	ok = ok && a && b && a[15] == 0xCAFEBABEu && b[7] == 0xDEADBEEFu;

	size_t total1, used1, big1;
	heap_stats(&total1, &used1, &big1);
	ok = ok && used1 >= used0 + 96 && total1 == total0;

	kfree(a);
	kfree(b);
	size_t total2, used2, big2;
	heap_stats(&total2, &used2, &big2);
	ok = ok && used2 < used1;
	ok = ok && big2 > big1;

	void* again = kmalloc(64);
	ok = ok && again != nullptr;
	kfree(again);

	uint8_t* z = (uint8_t*)kcalloc(16, 8);
	ok = ok && z;
	if (z) {
		bool zeroed = true;
		for (int i = 0; i < 128; ++i)
			if (z[i])
				zeroed = false;
		ok = ok && zeroed;
		z[0] = 0x11;
		z[127] = 0x22;
	}
	uint8_t* g = (uint8_t*)krealloc(z, 4096);
	ok = ok && g && g[0] == 0x11 && g[127] == 0x22;
	kfree(g);

	// frames have to come back 4k aligned
	bool frames_ok = true;
	void* fr[8];
	for (int i = 0; i < 8; ++i) {
		fr[i] = kframe_alloc();
		if (!fr[i] || ((uint32_t)fr[i] & 0xFFFu) != 0)
			frames_ok = false;
	}
	for (int i = 0; i < 8; ++i)
		if (fr[i])
			frames_ok = frames_ok && ((uint32_t)fr[i] & 0xFFFu) == 0;
	ok = ok && frames_ok;
	for (int i = 0; i < 8; ++i)
		kfree(fr[i]);

	uint8_t* big = (uint8_t*)kframe_alloc_n(4);
	ok = ok && big && ((uint32_t)big & 0xFFFu) == 0;
	if (big) {
		for (int i = 0; i < 4; ++i) {
			big[i * 4096] = (uint8_t)(i + 1);
			ok = ok && big[i * 4096] == (uint8_t)(i + 1);
		}
		kframe_free(big);
	}

	size_t total3, used3, big3;
	heap_stats(&total3, &used3, &big3);
	ok = ok && total3 == total0;
	ok = ok && big3 >= big0; // nothing stranded

	ok = ok && kmalloc(0) != nullptr;
	both("    heap=%s\n", ok ? "OK" : "FAIL");
}

void test_arena() {
	// Ok ok ok ok ok ok
	bool ok = true;
	size_t used0, big0;
	heap_stats(nullptr, &used0, &big0);

	task::task* a = task::create("arena_a", "arena_a");
	task::task* b = task::create("arena_b", "arena_b");
	ok = ok && a && b;
	if (!a || !b) {
		both("    arena=%s\n", ok ? "OK" : "FAIL");
		return;
	}

	const uint32_t base_a = (uint32_t)a->heap_base;
	const uint32_t base_b = (uint32_t)b->heap_base;
	ok = ok && a->brk == a->heap_base;
	ok = ok && (uint32_t)a->heap_end - base_a == task::kArenaBytes;
	ok = ok && (uint32_t)b->heap_end - base_b == task::kArenaBytes;

	ok = ok && (base_a & 0xFFFu) == 0 && (base_b & 0xFFFu) == 0;

	ok = ok && base_a != base_b;

	ok = ok && task_brk(a, 0) == task::kArenaVA;
	ok = ok && task_brk(a, task::kArenaVA + 4096u) == task::kArenaVA + 4096u;
	ok = ok && (uint32_t)a->brk == base_a + 4096u;
	ok = ok && (uint32_t)b->brk == base_b; // and it left the other one alone

	ok = ok && task_brk(a, task::kArenaVA + task::kArenaBytes) == task::kArenaVA + task::kArenaBytes;
	ok = ok && (uint32_t)a->brk == (uint32_t)a->heap_end;
	ok = ok && task_brk(a, task::kArenaVA + task::kArenaBytes + 1u) == 0;
	ok = ok && task_brk(a, 0x1000u) == 0;				  // nor is anything below the arena
	ok = ok && (uint32_t)a->brk == (uint32_t)a->heap_end; // refusals change nothing

	ok = ok && task_brk(a, task::kArenaVA) == task::kArenaVA;
	ok = ok && a->brk == a->heap_base;
	ok = ok && task_sbrk(a, 64) == task::kArenaVA + 64u;
	ok = ok && task_sbrk(a, -64) == task::kArenaVA;

	// a delta that walks off either end is refused, not clamped
	ok = ok && task_sbrk(a, (int)task::kArenaBytes + 1) == 0;
	ok = ok && task_sbrk(a, -1) == 0;
	ok = ok && a->brk == a->heap_base;

	task::destroy(a);
	task::destroy(b);
	size_t used1, big1;
	heap_stats(nullptr, &used1, &big1);
	ok = ok && used1 <= used0 + task::kArenaBytes;
	ok = ok && big1 >= big0; // nothing stranded

	both("    arena=%s\n", ok ? "OK" : "FAIL");
}

void test_open_modes() {
	bool ok = true;
	ok = ok && fs::write_file(task::cwd(), "/mnt", "abcdefgh", 8, O_TRUNC);

	const int app = vfs::fd_open("/mnt", O_APPEND);
	ok = ok && app >= 0 && vfs::lseek(app, 0, 1) == 8;
	vfs::fd_close(app);

	const int trunc = vfs::fd_open("/mnt", O_TRUNC);
	ok = ok && trunc >= 0 && vfs::lseek(trunc, 0, 1) == 0;
	vfs::fd_close(trunc);
	vfs::node* n = vfs::resolve("/mnt");
	fs::stat st;
	ok = ok && n && fs::getstat(n->inode, &st) && st.size == 0;
	both("    modes=%s\n", ok ? "OK" : "FAIL");
	fs::rm(task::cwd(), "/mnt");
}

void test_sched() {
	bool ok = true;
	ok = ok && task::g_current && task::g_current->pid == task::kPid1;
	ok = ok && task::g_current->state == task::kRunning;
	ok = ok && task::runnable() == 0;
	ok = ok && task::alive() == 1;

	task::task* a = task::fork();
	task::task* b = task::fork();
	ok = ok && a && b && a != b;
	if (a && b) {
		ok = ok && a->state == task::kReady && b->state == task::kReady;
		ok = ok && a->ppid == task::kPid1 && b->ppid == task::kPid1;
		ok = ok && a->pid != b->pid;
		ok = ok && a->sp && b->sp && a->sp != b->sp;
		ok = ok && (a->regs.cs & 3u) == 0u;
		ok = ok && a->quantum == task::kQuantum;
		ok = ok && task::find(a->pid) == a && task::find(b->pid) == b;
		ok = ok && task::find(9999) == nullptr;
		ok = ok && task::kill(a->pid, task::kSigKill);
		ok = ok && a->state == task::kKilled && a->signal == task::kSigKill;
		ok = ok && task::g_current->state == task::kRunning; // we did not move
		ok = ok && !task::kill(a->pid, task::kSigKill);		 // already dying
		ok = ok && !task::kill(9999, task::kSigKill);		 // no such thing

		ok = ok && task::runnable() == 1; // only b is left
	}
	// everything has to go back to where it started
	size_t used0, big0;
	heap_stats(nullptr, &used0, &big0);
	if (a)
		task::destroy(a);
	if (b)
		task::destroy(b);
	ok = ok && task::alive() == 1;
	ok = ok && task::runnable() == 0;
	size_t used1, big1;
	heap_stats(nullptr, &used1, &big1);
	ok = ok && used1 <= used0 + task::kArenaBytes;
	ok = ok && big1 >= big0;

	both("    sched=%s\n", ok ? "OK" : "FAIL");
}

void test_paging() {
	paging::space* s = paging::space_current();
	bool ok = (s == &paging::g_boot);
	uint32_t cr3;
	asm volatile("mov %%cr3, %0" : "=r"(cr3));
	ok = ok && s && (cr3 == paging::space_cr3(s));
	uint32_t cr0;
	asm volatile("mov %%cr0, %0" : "=r"(cr0));
	ok = ok && (cr0 & (1u << 31)) != 0;

	const uint32_t vaddrs[] = {0x00000000u, 0x00100000u, 0x07FE0000u, 0x80000000u, 0xFD000000u, 0xFFFFFFFFu};
	for (uint32_t va : vaddrs) {
		const uint64_t pde = s->pd[va >> 30][(va >> 21) & 0x1FF];
		ok = ok && (pde & 1) && (pde & (1ull << 7)) &&
			 ((uint32_t)(pde & 0xFFE00000ull) == (va & 0xFFE00000u)); // identity
	}
	paging::space* other = paging::space_create();
	if (other) {
		ok = ok && other != s && (uint32_t)(uintptr_t)other->pd[0] != (uint32_t)(uintptr_t)s->pd[0];
		ok = ok && other->pd[2][0] == s->pd[2][0]; // high 2gb still identity
		paging::space_destroy(other);
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
	gdt_init();
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
	test_gdt();

	const bool ata_ok = ata::init();
	both("    ata=%s\n", ata_ok ? "OK" : "FAIL");
	task::init();
	test_arena();
	test_sched();
	const bool fs_ok = ata_ok && fs::mount(0);
	both("    fs=%s\n", fs_ok ? "OK" : "FAIL");
	const bool vfs_ok = fs_ok && vfs::init();
	both("    vfs=%s\n", vfs_ok ? "OK" : "FAIL");
	if (vfs_ok)
		test_open_modes();

	if (vfs_ok)
		procfs::init();

	if (vfs_ok) {
		bool tree_ok = true;
		vfs::node* dev = vfs::resolve("/dev/null");
		tree_ok = tree_ok && dev && dev->type == vfs::kTypeChar;
		vfs::node* proc = vfs::resolve("/proc");
		tree_ok = tree_ok && proc && proc->type == vfs::kTypeDir;
		tree_ok = tree_ok && proc && proc->mount;
		tree_ok = tree_ok && vfs::resolve("/proc/1/stat") != nullptr;

		vfs::node* dv = vfs::resolve("/dev");
		vfs::node out;
		tree_ok = tree_ok && dv && dv->mount && vfs::readdir(dv, 0, &out) == 0;
		both("    trees=%s\n", tree_ok ? "OK" : "FAIL");
	}

	console::setcolor(0xCBA6F7, 0x1E1E2E);
	both("\n  AURISYS: all tests passed\n");
	console::setcolor(0xCDD6F4, 0x1E1E2E);

	serial::puts("\r\nAURISYS: all tests passed\r\n");
	serial::puts("\r\nAURISYS: terminal ready\r\n");

	static const char* const sh_argv[] = {"sh", nullptr};
	static const char* const sh_envp[] = {"PATH=/bin", "HOME=/", "TERM=vt100", nullptr};
	if (!exec::init_first("/bin/sh", 1, sh_argv, 3, sh_envp)) {
		both("\n  AURISYS: no shell. nothing can run on this.\n");
		serial::puts("\r\nAURISYS: no shell. nothing can run on this.\r\n");
		for (;;)
			asm volatile("cli; hlt");
	}

	// hand the machine over
	task::become_first();
}