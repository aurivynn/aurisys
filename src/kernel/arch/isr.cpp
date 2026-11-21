#include "arch/isr.h"

#include "arch/paging.h"
#include "drivers/pic.h"
#include "lib/klog.h"
#include "lib/panic.h"
#include "task.h"

#include <stdint.h>

static irq_handler_t g_handlers[16] = {};

// rpl 3 lives in the low two bits of the selector the cpu pushed
static bool from_user(const Registers* r) { return (r->cs & 3) == 3; }

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

static uint32_t fault_signal(int vec) {
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

static bool retry_cow_fault(const Registers* r) {
	if (r->int_no != 14)
		return false;
	uint32_t cr2 = 0;
	asm volatile("mov %%cr2, %0" : "=r"(cr2));
	task::task* t = task::g_current;
	if (!t)
		return false;
	return paging::space_cow_fault(t->sp, cr2);
}

[[noreturn]] static void kill_user_fault(const Registers* r) {
	uint32_t cr2 = 0;
	if (r->int_no == 14)
		asm volatile("mov %%cr2, %0" : "=r"(cr2));
	task::task* t = task::g_current;
	klog::printf("process %u (%s) died: %s at ip=%08x sp=%08x", t ? t->pid : 0u, t ? t->name : "?",
				 fault_name((int)r->int_no), r->eip, r->user_esp);
	if (r->int_no == 14)
		klog::printf(" addr=%08x err=%08x", cr2, r->err_code);
	klog::printf("\n");
	if (t) {
		t->signal = fault_signal((int)r->int_no);
		t->state = task::kKilled;
	}
	task::exit(128);
}

void irq_install(int irq, irq_handler_t fn) {
	if (irq >= 0 && irq < 16)
		g_handlers[irq] = fn;
}

extern "C" void isr_common(Registers* r) {
	if (r->int_no < 32) {
		if (from_user(r)) {
			if (retry_cow_fault(r))
				return;
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
