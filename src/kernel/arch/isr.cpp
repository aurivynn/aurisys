#include "arch/isr.h"

#include "drivers/console.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "lib/panic.h"
#include "lib/print.h"
#include "task.h"

#include <stdarg.h>
#include <stdint.h>

namespace {

irq_handler_t g_handlers[16] = {};

// rpl 3 lives in the low two bits of the selector the cpu pushed
bool from_user(const Registers* r) { return (r->cs & 3) == 3; }

void emit_both(char c) {
	serial::putc(c);
	console::putchar(c);
}

void note(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&emit_both, fmt, ap);
	va_end(ap);
}

const char* fault_name(int vec) {
	switch (vec) {
	case 6:
		return "invalid opcode";
	case 8:
		return "double fault";
	case 11:
		return "segment not present";
	case 12:
		return "stack fault";
	case 13:
		return "general protection";
	case 14:
		return "page fault";
	default:
		return "exception";
	}
}

uint32_t fault_signal(int vec) {
	switch (vec) {
	case 6:
		return 4; // SIGILL
	case 8:
		return 7; // SIGBUS. a double fault has nowhere left to report itself
	case 11:
	case 12:
	case 13:
		return 7; // SIGBUS
	case 14:
		return task::kSigSegv;
	default:
		return 4;
	}
}

// a bad instruction in a process is that process's problem, not the kernels. it dies of the signal the exception maps
// to, and the scheduler carries on with whoever is next
[[noreturn]] void kill_user_fault(const Registers* r) {
	uint32_t cr2 = 0;
	if (r->int_no == 14)
		asm volatile("mov %%cr2, %0" : "=r"(cr2));
	task::task* t = task::g_current;
	note("process %u (%s) died: %s at ip=%08x sp=%08x", t ? t->pid : 0u, t ? t->name : "?", fault_name((int)r->int_no),
		 r->eip, r->user_esp);
	if (r->int_no == 14)
		note(" addr=%08x err=%08x", cr2, r->err_code);
	note("\n");
	if (t)
		t->signal = fault_signal((int)r->int_no);
	task::exit(128);
}

} // namespace

void irq_install(int irq, irq_handler_t fn) {
	if (irq >= 0 && irq < 16)
		g_handlers[irq] = fn;
}

extern "C" void isr_common(Registers* r) {
	if (r->int_no < 32) {
		if (from_user(r)) {
			kill_user_fault(r); // does not return
			return;
		}
		panic_regs("exception", r); // the kernel itself is in trouble
		return;
	}
	const int irq = (int)r->int_no - 32;
	if (g_handlers[irq])
		g_handlers[irq](r);
	if (irq == 0) {
		task::on_tick();
		pic::eoi(irq);
		if (task::preempt(r))
			return;
		return;
	}
	pic::eoi(irq);
}
