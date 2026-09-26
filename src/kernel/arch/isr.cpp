#include "arch/isr.h"

#include "drivers/pic.h"
#include "lib/panic.h"

namespace {

irq_handler_t g_handlers[16] = {};

} // namespace

void irq_install(int irq, irq_handler_t fn) {
	if (irq >= 0 && irq < 16)
		g_handlers[irq] = fn;
}

extern "C" void isr_common(Registers* r) {
	if (r->int_no < 32) {
		panic_regs("exception", r); // never returns
		return;
	}
	const int irq = (int)r->int_no - 32;
	if (g_handlers[irq])
		g_handlers[irq](r);
	pic::eoi(irq);
}