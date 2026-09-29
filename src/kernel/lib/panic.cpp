#include "lib/panic.h"

#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "lib/print.h"
#include "task.h"

#include <stdarg.h>
#include <stdint.h>

namespace {

inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

void emit(char c) {
	serial::putc(c);
	console::putchar(c);
}

void bothf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&emit, fmt, ap);
	va_end(ap);
}

const char* fault_name(int vec) {
	switch (vec) {
	case 0:
		return "divide error";
	case 1:
		return "debug";
	case 2:
		return "nmi";
	case 3:
		return "breakpoint";
	case 4:
		return "overflow";
	case 6:
		return "invalid opcode";
	case 7:
		return "no fpu";
	case 8:
		return "double fault";
	case 10:
		return "invalid tss";
	case 11:
		return "segment not present";
	case 12:
		return "stack fault";
	case 13:
		return "general protection";
	case 14:
		return "page fault";
	case 16:
		return "x87 fpu error";
	case 17:
		return "alignment check";
	case 18:
		return "machine check";
	default:
		return "reserved";
	}
}

} // namespace

[[noreturn]] void panic_regs(const char* why, Registers* r) {
	asm volatile("cli");
	pic::mask_all();

	fb::clear(0x1E1E2E);
	console::init();
	console::setcolor(0xF38BA8, 0x1E1E2E);
	bothf("PANIC: %s", why);
	if (r && r->int_no < 32)
		bothf(" %u (%s)", r->int_no, fault_name((int)r->int_no));
	bothf("\n");
	console::setcolor(0xCDD6F4, 0x1E1E2E);

	if (r) {
		bothf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", r->eax, r->ebx, r->ecx, r->edx);
		bothf("ESI=%08x EDI=%08x EBP=%08x EIP=%08x\n", r->esi, r->edi, r->ebp, r->eip);
		uint32_t cr2 = 0;
		if (r->int_no == 14)
			asm volatile("mov %%cr2, %0" : "=r"(cr2));
		bothf("CS=%04x EFLAGS=%08x ERR=%08x CR2=%08x\n", r->cs, r->eflags, r->err_code, cr2);
	}

	task::task* t = task::g_current;
	if (t) {
		bothf("TASK pid=%u ppid=%u state=%d in_syscall=%d sig_pending=%u sig_active=%u\n", t->pid, t->ppid,
			  (int)t->state, t->in_syscall ? 1 : 0, t->sig_pending, t->sig_active);
		bothf("TASK saved_cs=%04x saved_eip=%08x user_esp=%08x kstack=%08x ksp=%08x\n", t->regs.cs, t->regs.eip,
			  t->regs.user_esp, t->kstack, t->kslot[0]);
		bothf("TASK name=%s\n", t->name);
	} else {
		bothf("TASK none\n");
	}

	bothf("-- halted --\n");

	outb(0xf4, 0);
	for (;;)
		asm volatile("hlt");
}

[[noreturn]] void panic(const char* msg) { panic_regs(msg, nullptr); }