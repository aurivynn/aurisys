#include "arch/isr.h"

#include "drivers/console.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "lib/panic.h"
#include "lib/print.h"
#include "task.h"

#include <stdarg.h>
#include <stdint.h>

extern "C" void task_exit_to_shell();

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

// a bad instruction in a process is that process's problem & not th kernels
void kill_user_fault(const Registers* r) {
	uint32_t cr2 = 0;
	if (r->int_no == 14)
		asm volatile("mov %%cr2, %0" : "=r"(cr2));
	task::task* t = task::g_current;
	note("process %u (%s) died: %s at ip=%08x sp=%08x", t ? t->pid : 0u, t ? t->name : "?", fault_name((int)r->int_no),
		 r->eip, r->esp_dummy);
	if (r->int_no == 14)
		note(" addr=%08x err=%08x", cr2, r->err_code);
	note("\n");
	if (t)
		t->state = task::kKilled;
	task_exit_to_shell();
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
	pic::eoi(irq);
}
