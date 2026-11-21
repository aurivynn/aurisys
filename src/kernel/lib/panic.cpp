#include "lib/panic.h"

#include "arch/io.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/pic.h"
#include "lib/klog.h"
#include "task.h"

#include <stdint.h>

[[noreturn]] void panic_regs(const char* why, Registers* r) {
	asm volatile("cli");
	pic::mask_all();

	fb::clear(0x1E1E2E);
	console::init();
	console::setcolor(0xF38BA8, 0x1E1E2E);
	klog::printf("PANIC: %s", why);
	if (r && r->int_no < 32)
		klog::printf(" %u (%s)", r->int_no, fault_name((int)r->int_no));
	klog::printf("\n");
	console::setcolor(0xCDD6F4, 0x1E1E2E);

	if (r) {
		klog::printf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", r->eax, r->ebx, r->ecx, r->edx);
		klog::printf("ESI=%08x EDI=%08x EBP=%08x EIP=%08x\n", r->esi, r->edi, r->ebp, r->eip);
		uint32_t cr2 = 0;
		if (r->int_no == 14)
			asm volatile("mov %%cr2, %0" : "=r"(cr2));
		klog::printf("CS=%04x EFLAGS=%08x ERR=%08x CR2=%08x\n", r->cs, r->eflags, r->err_code, cr2);
	}

	task::task* t = task::g_current;
	if (t) {
		klog::printf("TASK pid=%u ppid=%u state=%d in_syscall=%d sig_pending=%u sig_active=%u\n", t->pid, t->ppid,
					 (int)t->state, t->in_syscall ? 1 : 0, t->sig_pending, t->sig_active);
		klog::printf("TASK saved_cs=%04x saved_eip=%08x user_esp=%08x kstack=%08x ksp=%08x\n", t->regs.cs, t->regs.eip,
					 t->regs.user_esp, t->kstack, t->kslot[0]);
		klog::printf("TASK name=%s\n", t->name);
	} else {
		klog::printf("TASK none\n");
	}

	klog::printf("-- halted --\n");

	io::outb(0xf4, 0);
	for (;;)
		asm volatile("hlt");
}

[[noreturn]] void panic(const char* msg) { panic_regs(msg, nullptr); }
