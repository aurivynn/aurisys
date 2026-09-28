#include "task.h"

#include "arch/gdt.h"
#include "fs.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "shell/terminal.h"

#include <stdint.h>

namespace task {

task g_tasks[kMaxTask];
task* g_current = nullptr;

namespace {
task* g_foreground = nullptr;
} // namespace

void set_foreground(task* t) { g_foreground = t; }
task* foreground() { return g_foreground; }

void clear_foreground(task* t) {
	if (g_foreground == t)
		g_foreground = nullptr;
}

namespace {

constexpr uint32_t kKernelStackBytes = 16384;

constexpr uint32_t kKernelSlack = 4096u;
constexpr uint32_t kKernelAlloc = kKernelStackBytes + kKernelSlack;

int free_slot() {
	for (int i = 1; i < kMaxTask; ++i)
		if (g_tasks[i].state == kFree)
			return i;
	return -1;
}

static void purge_notes(uint32_t pid) {
	for (int i = 0; i < kMaxTask; ++i) {
		task* p = &g_tasks[i];
		uint32_t n = 0;
		for (uint32_t k = 0; k < p->done_n; ++k) {
			if (p->done_pid[k] == pid)
				continue;
			p->done_pid[n] = p->done_pid[k];
			p->done_code[n] = p->done_code[k];
			++n;
		}
		p->done_n = (uint8_t)n;
	}
}

int count_fds(const vfs::ofile* fd) {
	int n = 0;
	for (int i = 0; i < vfs::kMaxFd; ++i)
		if (fd[i].n.type)
			++n;
	return n;
}

} // namespace

task* find(uint32_t pid) {
	for (int i = 0; i < kMaxTask; ++i)
		if (g_tasks[i].state != kFree && g_tasks[i].pid == pid)
			return &g_tasks[i];
	return nullptr;
}

task* by_pid(uint32_t pid) { return find(pid); }

void arm(task* t) {
	if (t)
		t->armed = true;
}

bool give_frames(task* t) {
	if (!t)
		return false;
	t->code_base = (uint32_t*)kframe_alloc_n(kCodeBytes / 4096u);
	t->stack_base = (uint32_t*)kframe_alloc_n(kStackBytes / 4096u);
	t->heap_base = (uint32_t*)kframe_alloc_n(kArenaBytes / 4096u);
	if (!t->code_base || !t->stack_base || !t->heap_base) {
		kframe_free(t->code_base);
		kframe_free(t->stack_base);
		kframe_free(t->heap_base);
		t->code_base = t->stack_base = t->heap_base = nullptr;
		return false;
	}
	t->code_bytes = kCodeBytes;
	t->heap_end = t->heap_base + kArenaBytes / 4u;
	t->brk = t->heap_base;
	return true;
}

task* create(const char* name, const char* argv0) {
	const int slot = free_slot();
	if (slot < 0)
		return nullptr;
	task* t = &g_tasks[slot];
	memset(t, 0, sizeof *t);
	t->sp = paging::space_create();
	if (!t->sp)
		return nullptr;
	t->kstack = (uint32_t*)kframe_alloc_n(kKernelAlloc / 4096u);
	if (!t->kstack) {
		paging::space_destroy(t->sp);
		return nullptr;
	}
	if (!give_frames(t)) {
		kfree(t->kstack);
		paging::space_destroy(t->sp);
		return nullptr;
	}
	t->owns_frames = true;
	t->kstack = (uint32_t*)((uint32_t)t->kstack + kKernelStackBytes);
	t->quantum = kQuantum;

	t->pid = (uint32_t)(slot) + 1u;
	t->ppid = g_current ? g_current->pid : 0;

	t->state = kReady;
	t->armed = false;

	t->regs.cs = 0;
	strncpy(t->name, name ? name : "?", sizeof t->name - 1);
	// a child inherits the table it was created from, so the console ops come along.
	if (g_current) {
		memcpy(t->fd, g_current->fd, sizeof t->fd);
		t->nfd = (uint8_t)count_fds(t->fd);
	}

	if (g_current)
		memcpy(t->cwd, g_current->cwd, sizeof t->cwd);
	else
		strcpy(t->cwd, "/");
	(void)argv0;
	return t;
}

void destroy(task* t) {
	if (!t || t->state == kFree)
		return;

	const bool shared = !t->owns_frames;
	paging::space_destroy(t->sp);
	t->sp = nullptr;

	if (t->kstack) {
		kfree((void*)((uint32_t)t->kstack - kKernelStackBytes));
		t->kstack = nullptr;
	}

	if (shared) {
		// the parent still owns all of it
		t->heap_base = nullptr;
		t->heap_end = nullptr;
		t->brk = nullptr;
		t->code_base = nullptr;
		t->stack_base = nullptr;
	} else {
		kframe_free(t->heap_base);
		t->heap_base = nullptr;
		t->heap_end = nullptr;
		t->brk = nullptr;
		// the process image goes back with it
		kframe_free(t->code_base);
		t->code_base = nullptr;
		kframe_free(t->stack_base);
		t->stack_base = nullptr;
	}
	t->owns_frames = false;
	t->state = kFree;
}

static inline uint32_t to_va(task* t, uint32_t kaddr) { return kArenaVA + (kaddr - (uint32_t)t->heap_base); }

uint32_t task_brk(task* t, uint32_t addr) {
	if (!t || !t->brk)
		return 0;
	if (addr == 0)
		return to_va(t, (uint32_t)t->brk); // the plain query

	if (addr < kArenaVA || addr > kArenaVA + kArenaBytes)
		return 0;
	t->brk = (uint32_t*)((uint32_t)t->heap_base + (addr - kArenaVA));
	return addr;
}

uint32_t task_sbrk(task* t, int delta) {
	if (!t || !t->brk)
		return 0;
	const int64_t next = (int64_t)(uint32_t)t->brk + delta;
	if (next < (int64_t)(uint32_t)t->heap_base || next > (int64_t)(uint32_t)t->heap_end)
		return 0;
	t->brk = (uint32_t*)next;
	return to_va(t, (uint32_t)t->brk);
}

const char* cwd() { return g_current ? g_current->cwd : "/"; }

bool chdir(const char* path) {
	task* t = g_current;
	return t && fs::chdir(t->cwd, path, t->cwd, sizeof t->cwd);
}

task* init() {
	g_current = &g_tasks[0];
	g_tasks[0].state = kRunning;
	g_tasks[0].pid = kPid1;
	g_tasks[0].ppid = 0;
	g_tasks[0].sp = &paging::g_boot;

	g_tasks[0].kstack = (uint32_t*)kframe_alloc_n(kKernelAlloc / 4096u);
	if (g_tasks[0].kstack)
		g_tasks[0].kstack = (uint32_t*)((uint32_t)g_tasks[0].kstack + kKernelStackBytes);

	g_tasks[0].regs.cs = kSelKernCode;
	g_tasks[0].quantum = kQuantum;

	g_tasks[0].armed = true;
	strcpy(g_tasks[0].name, "init");
	return g_current;
}

const char* signal_name(uint32_t sig) {
	switch (sig) {
	case kSigTerm:
		return "SIGTERM";
	case kSigKill:
		return "SIGKILL";
	case kSigChild:
		return "SIGCHLD";
	case kSigSegv:
		return "SIGSEGV";
	case 4:
		return "SIGILL";
	case 7:
		return "SIGBUS";
	default:
		return "none";
	}
}

const char* state_name(uint32_t state) {
	switch (state) {
	case kFree:
		return "free";
	case kReady:
		return "ready";
	case kRunning:
		return "running";
	case kZombie:
		return "zombie";
	case kKilled:
		return "killed";
	case kBlocked:
		return "blocked";
	default:
		return "?";
	}
}

extern "C" void task_switch_asm(uint32_t user_esp, uint32_t kernel_esp, uint32_t from_user, uint32_t* slot);

namespace {

void push_frame(task* t, const Registers& r) {
	uint32_t* f = t->kstack - kFrWords;
	memcpy(f, &r, sizeof(Registers));
}

uint32_t resume_esp(const task* t) { return (uint32_t)(uintptr_t)(t->kstack - kFrWords); }

void switch_to(task* to) {
	uint32_t flags;
	asm volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");

	task* from = g_current;

	if (from && from->state == kRunning)
		from->state = kReady;
	to->state = kRunning;
	to->quantum = kQuantum;
	g_current = to;
	paging::space_switch(to->sp);

	if (to->kstack)
		gdt_set_kernel_stack((uint32_t)(uintptr_t)to->kstack);

	const uint32_t from_user = (!to->in_syscall && (to->regs.cs & 3u) == 3u) ? 1u : 0u;
	if (from_user) {
		deliver_pending(to, &to->regs);
		push_frame(to, to->regs);
	}
	task_switch_asm(from_user ? resume_esp(to) : 0, to->kslot[5], from_user, to->kslot);
}

task* pick() {
	const int from = g_current ? (int)(g_current - g_tasks) : 0;
	for (int i = 1; i <= kMaxTask; ++i) {
		const int s = (from + i) % kMaxTask;
		if (g_tasks[s].state == kReady && g_tasks[s].armed)
			return &g_tasks[s];
	}
	return nullptr;
}

} // namespace

extern "C" void task_switch_now() {
	reap();
	task* to = pick();

	if (to && (to->state == kZombie || to->state == kKilled))
		to = nullptr;
	if (!to) {
		if (g_current->state == kBlocked)
			__asm__ volatile("sti; hlt");
		return;
	}
	switch_to(to);
}

extern "C" void task_suspend_asm(uint32_t* slot);

void schedule() {
	task_suspend_asm(g_current->kslot);
	// nothing else was runnable
}

void yield() { schedule(); }

// off the run queue until somebody unblocks us
void block_on(task** slot) {
	if (!slot || !g_current)
		return;
	*slot = g_current;
	block();
	// the claim is over
	*slot = nullptr;
}

void block() {
	task* t = g_current;
	if (!t || t->state == kBlocked)
		return;

	t->state = kBlocked;
	t->in_syscall = true;
	if (t->wake_pending) {
		t->wake_pending = 0;
		t->state = kRunning;
		t->in_syscall = false;
		return;
	}
	schedule();
	t->wake_pending = 0;
}

void unblock(task* t) {
	if (!t)
		return;

	if (t->state == kBlocked) {
		t->state = kReady;
		return;
	}

	t->wake_pending = 1;
}

// ticks since boot
static uint32_t g_ticks = 0;
uint32_t ticks_now() { return g_ticks; }

void on_tick() {
	++g_ticks;
	for (int i = 0; i < kMaxTask; ++i) {
		task* s = &g_tasks[i];
		if (s->state == kBlocked && s->sleep_until && g_ticks >= s->sleep_until) {
			s->sleep_until = 0;
			unblock(s);
		}

		else if (s->state == kBlocked && s->sig_pending && !s->waiting)
			unblock(s);
	}

	task* t = g_current;
	if (t)
		++t->utime;
	if (serial::pending())
		kbd::wake();
}

// sleep for a number of ticks
int sleep_ticks(uint32_t ticks) {
	task* t = g_current;
	if (!t)
		return -kErrInval;

	if (!ticks)
		return 0;
	if (t->sig_pending)
		return -kErrIntr;
	t->sleep_until = ticks_now() + ticks;
	block();

	if (t->sig_pending) {
		t->sleep_until = 0;
		return -kErrIntr;
	}
	return 0;
}

bool preempt(Registers* r) {
	task* t = g_current;
	if (!t)
		return false;

	const bool dead = t->state == kZombie || t->state == kKilled;

	if (!dead) {
		if ((r->cs & 3u) != 3u)
			return false;
		if (t->quantum && --t->quantum)
			return false;
	}
	reap();
	task* to = pick();
	if (!to || to == t)
		return false;

	// a corpse is never coming back
	if (!dead) {
		t->regs = *r;
		t->in_syscall = false;
	}
	switch_to(to);
	return true;
}

int runnable() {
	int n = 0;
	for (int i = 1; i < kMaxTask; ++i)
		if (g_tasks[i].state == kReady)
			++n;
	return n;
}

int alive() {
	int n = 0;
	for (int i = 0; i < kMaxTask; ++i)
		if (g_tasks[i].state != kFree)
			++n;
	return n;
}

static bool deliver(task* t, Registers* r, uint32_t sig) {
	if (sig == 0 || sig >= (uint32_t)kSigMax)
		return false;
	const uint32_t handler = t->sig_handlers[sig];
	if (!handler)
		return false;

	if (t->sig_mask & (1u << sig))
		return false;

	uint32_t sp = r->user_esp;
	sp &= ~3u;
	if (sp < sizeof(sigframe) + 16u)
		return false;
	const uint32_t fp = sp - sizeof(sigframe);

	sigframe* f = (sigframe*)(uintptr_t)fp;
	memset(f, 0, sizeof *f);

	f->edi = r->edi;
	f->esi = r->esi;
	f->ebp = r->ebp;
	f->esp_dummy = r->esp_dummy;
	f->ebx = r->ebx;
	f->edx = r->edx;
	f->ecx = r->ecx;
	f->eax = r->eax;
	f->int_no = 0x80 | sig;
	f->err_code = 0;
	f->eip = r->eip;
	f->cs = r->cs;
	f->eflags = r->eflags;
	f->user_esp = r->user_esp;
	f->user_ss = r->user_ss;
	f->sig = sig;
	f->fp = fp;
	f->ret = fp + (uint32_t)offsetof(sigframe, tramp);
	f->arg0 = sig;
	f->arg1 = fp;

	uint8_t code[9] = {0xB8, 0, 0, 0, 0, 0xCD, 0x80, 0x0F, 0x0B};
	code[1] = (uint8_t)SYS_sigreturn;
	code[2] = (uint8_t)(SYS_sigreturn >> 8);
	code[3] = (uint8_t)(SYS_sigreturn >> 16);
	code[4] = (uint8_t)(SYS_sigreturn >> 24);
	memcpy(f->tramp, code, sizeof code);

	r->eip = handler;
	r->user_esp = fp + (uint32_t)offsetof(sigframe, ret);
	r->esp_dummy = r->user_esp;

	t->sig_saved_mask = t->sig_mask;
	t->sig_mask |= (1u << sig) | t->sig_masks[sig];
	t->sig_active = sig;
	return true;
}

bool kill(uint32_t pid, uint32_t sig) {
	task* t = find(pid);
	if (!t)
		return false;

	if (t->state == kZombie || t->state == kKilled)
		return false;

	if (sig != kSigKill && sig < (uint32_t)kSigMax && t->sig_handlers[sig]) {
		t->sig_pending = sig;
		if (t->state == kBlocked)
			unblock(t);
		return true;
	}

	t->signal = sig;
	t->state = kKilled;
	if (t == g_current)
		exit(128 + (int)sig);
	return true;
}

void sigreturn(Registers* r) {
	task* t = g_current;
	if (!t || !r)
		return;
	const uint32_t sig = t->sig_active;
	if (!sig || sig >= (uint32_t)kSigMax)
		return;

	const uint32_t sp = r->user_esp;
	if (sp < (uint32_t)offsetof(sigframe, arg0)) {
		t->signal = kSigSegv;
		exit(128 + kSigSegv);
	}
	sigframe* f = (sigframe*)(uintptr_t)(sp - (uint32_t)offsetof(sigframe, arg0));

	r->edi = f->edi;
	r->esi = f->esi;
	r->ebp = f->ebp;
	r->esp_dummy = f->esp_dummy;
	r->ebx = f->ebx;
	r->edx = f->edx;
	r->ecx = f->ecx;
	r->eax = f->eax;
	r->eip = f->eip;
	r->cs = f->cs;
	r->eflags = f->eflags;
	r->user_esp = f->user_esp;
	r->user_ss = f->user_ss;

	t->sig_mask = t->sig_saved_mask;
	t->sig_active = 0;

	const uint32_t next = t->sig_pending;
	t->sig_pending = 0;
	if (next && next < (uint32_t)kSigMax)
		deliver(t, r, next);
}

void deliver_pending(task* t, Registers* r) {
	if (!t || !r || !t->sig_pending || t->sig_active)
		return;
	const uint32_t sig = t->sig_pending;
	t->sig_pending = 0;

	if (sig < (uint32_t)kSigMax && t->sig_handlers[sig])
		deliver(t, r, sig);
}

static void announce(task* t) {
	task* parent = find(t->ppid);
	if (!parent || parent == t) {

		return;
	}
	const uint32_t n = parent->done_n;
	if (n >= (uint32_t)kMaxTask) {
		return;
	}
	parent->done_pid[n] = t->pid;
	parent->done_code[n] = t->signal ? 128u + t->signal : t->exit_code;
	parent->done_n = (uint8_t)(n + 1u);

	if (parent->waiting)
		unblock(parent);
}

void reap() {
	for (int i = 1; i < kMaxTask; ++i) {
		task* t = &g_tasks[i];
		if (t->state == kKilled) {
			t->exit_code = 128 + (int)t->signal;
			t->state = kZombie;

			announce(t);
		}
		if (t->state != kZombie || t == g_current)
			continue;
		destroy(t);
	}
}

static int32_t take_done(task* t, int32_t pid, uint32_t* code) {
	for (uint32_t i = 0; i < t->done_n; ++i) {
		if (pid && t->done_pid[i] != (uint32_t)pid)
			continue;
		const uint32_t got = t->done_pid[i];
		if (code)
			*code = t->done_code[i];
		// the last one moves into the hole so the list stays packed
		t->done_pid[i] = t->done_pid[t->done_n - 1u];
		t->done_code[i] = t->done_code[t->done_n - 1u];
		--t->done_n;
		return (int32_t)got;
	}
	return 0;
}

int32_t wait_for(int32_t pid, uint32_t* code) {
	task* t = g_current;
	if (!t)
		return -kErrInval;

	for (;;) {
		t->wake_pending = 0;

		reap();
		const int32_t had = take_done(t, pid, code);
		if (had) {

			return had;
		}

		if (pid) {
			task* want = find((uint32_t)pid);
			if (!want) {
				return -kErrSrch;
			}
			if (want->ppid != t->pid) {

				return -kErrChild;
			}
		}

		t->waiting = true;
		t->wait_pid = pid;

		block();

		t->waiting = false;

		if (t->sig_pending) {
			return -kErrIntr;
		}
	}
}

int console_key() {
	int c = kbd::poll();
	if (c < 0)
		c = (int)serial::recv();
	if (c != 0x03)
		return c;

	if (g_foreground && g_foreground->state != kRunning)
		g_foreground = nullptr;

	task* fg = g_foreground;

	if (fg && fg != g_current) {
		kill(fg->pid, kSigInt);
		return -1;
	}
	if (fg && fg == g_current && g_current->pid != kPid1) {
		kill(fg->pid, kSigInt);
		return -1;
	}

	return c;
}

uint32_t take_signal(uint32_t for_pid) {
	task* t = g_current;
	if (!t)
		return kSigNone;
	for (uint32_t i = 0; i < t->done_n; ++i) {
		if (for_pid && t->done_pid[i] != for_pid)
			continue;
		t->done_pid[i] = t->done_pid[t->done_n - 1u];
		--t->done_n;
		return kSigChild;
	}
	return kSigNone;
}

task* fork() { return create("child", "child"); }

[[noreturn]] void resume_after_exec() {
	task* t = g_current;
	if (t) {
		t->in_syscall = false;
		t->state = kRunning;
	}

	for (;;)
		schedule();
}

uint32_t fork_user(const Registers* frame) {
	task* p = g_current;
	if (!p || !p->sp) {
		return 0;
	}

	if ((frame->cs & 3u) != 3u) {
		return 0;
	}

	const int slot = free_slot();
	if (slot < 0) {
		return 0;
	}
	purge_notes((uint32_t)slot + 1u);
	task* t = &g_tasks[slot];
	memset(t, 0, sizeof *t);

	t->sp = paging::space_create();
	if (!t->sp) {
		return 0;
	}
	if (!paging::space_share_user(t->sp, p->sp)) {
		paging::space_destroy(t->sp);
		return 0;
	}
	t->kstack = (uint32_t*)kframe_alloc_n(kKernelAlloc / 4096u);
	if (!t->kstack) {
		memset(t, 0, sizeof *t);
		return 0;
	}
	t->kstack = (uint32_t*)((uint32_t)t->kstack + kKernelStackBytes);
	t->quantum = kQuantum;
	t->pid = (uint32_t)(slot) + 1u;
	t->ppid = p->pid;
	strncpy(t->name, "fork", sizeof t->name - 1);

	memcpy(t->fd, p->fd, sizeof t->fd);
	for (int i = 0; i < vfs::kMaxFd; ++i)
		if (t->fd[i].n.type)
			vfs::fd_share(&t->fd[i]);
	t->nfd = (uint8_t)count_fds(t->fd);
	memcpy(t->cwd, p->cwd, sizeof t->cwd);

	t->owns_frames = false;
	t->heap_base = p->heap_base;
	t->heap_end = p->heap_end;
	t->brk = p->brk;
	t->code_base = p->code_base;
	t->code_bytes = p->code_bytes;
	t->stack_base = p->stack_base;

	t->regs = *frame;
	t->regs.eax = 0;
	t->state = kReady;
	t->armed = true;

	return t->pid;
}

[[noreturn]] void exit(int code) {
	task* t = g_current;
	if (t) {

		t->exit_code = (uint32_t)code;
		if (t->state != kKilled)
			t->signal = kSigNone;
		t->state = kZombie;
		announce(t);
		t->in_syscall = true;
	}
	schedule();

	for (;;)
		asm volatile("hlt");
}

int fd_read_kernel(int fd, void* buf, uint32_t len) { return vfs::fd_read(fd, buf, len); }
int fd_write_kernel(int fd, const void* buf, uint32_t len) { return vfs::fd_write(fd, buf, len); }

} // namespace task
