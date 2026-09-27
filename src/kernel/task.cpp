#include "task.h"

#include "arch/gdt.h"
#include "fs.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "shell/terminal.h"

#include <stdint.h>

namespace task {

task g_tasks[kMaxTask];
task* g_current = nullptr;

namespace {

constexpr uint32_t kKernelStackBytes = 16384;

int free_slot() {
	for (int i = 1; i < kMaxTask; ++i)
		if (g_tasks[i].state == kFree)
			return i;
	return -1;
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

task* create(const char* name, const char* argv0) {
	const int slot = free_slot();
	if (slot < 0)
		return nullptr;
	task* t = &g_tasks[slot];
	memset(t, 0, sizeof *t);
	t->sp = paging::space_create();
	if (!t->sp)
		return nullptr;
	t->kstack = (uint32_t*)kframe_alloc_n(kKernelStackBytes / 4096u);
	if (!t->kstack) {
		paging::space_destroy(t->sp);
		return nullptr;
	}

	t->heap_base = (uint32_t*)kframe_alloc_n(kArenaBytes / 4096u);
	if (!t->heap_base) {
		kfree(t->kstack);
		paging::space_destroy(t->sp);
		return nullptr;
	}
	t->heap_end = t->heap_base + kArenaBytes / 4u;
	t->brk = t->heap_base;

	t->code_bytes = kCodeBytes;
	t->code_base = (uint32_t*)kframe_alloc_n(kCodeBytes / 4096u);
	if (!t->code_base) {
		kframe_free(t->heap_base);
		kfree(t->kstack);
		paging::space_destroy(t->sp);
		return nullptr;
	}
	t->stack_base = (uint32_t*)kframe_alloc_n(kStackBytes / 4096u);
	if (!t->stack_base) {
		kframe_free(t->code_base);
		kframe_free(t->heap_base);
		kfree(t->kstack);
		paging::space_destroy(t->sp);
		return nullptr;
	}

	t->kstack = (uint32_t*)((uint32_t)t->kstack + kKernelStackBytes);
	t->quantum = kQuantum;

	t->pid = (uint32_t)(slot) + 1u;
	t->ppid = g_current ? g_current->pid : 0;
	t->state = kReady;

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

const char* cwd() {
	task* t = g_current;
	return t ? t->cwd : "/";
}

bool chdir(const char* path) {
	task* t = g_current;
	return t && fs::chdir(t->cwd, path, t->cwd, sizeof t->cwd);
}

void destroy(task* t) {
	if (!t || t->state == kFree)
		return;
	paging::space_destroy(t->sp);
	t->sp = nullptr;

	if (t->kstack) {
		kfree((void*)((uint32_t)t->kstack - kKernelStackBytes));
		t->kstack = nullptr;
	}

	kframe_free(t->heap_base);
	t->heap_base = nullptr;
	t->heap_end = nullptr;
	t->brk = nullptr;
	// the process image goes back with it
	kframe_free(t->code_base);
	t->code_base = nullptr;
	kframe_free(t->stack_base);
	t->stack_base = nullptr;
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

task* init() {
	g_current = &g_tasks[0];
	g_tasks[0].state = kRunning;
	g_tasks[0].pid = kPid1;
	g_tasks[0].ppid = 0;
	g_tasks[0].sp = &paging::g_boot;

	g_tasks[0].regs.cs = kSelKernCode;
	g_tasks[0].quantum = kQuantum;
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
	case kRunning:
		return "R";
	case kReady:
		return "S";
	case kBlocked:
		return "D";
	case kZombie:
		return "Z";
	case kKilled:
		return "T";
	default:
		return "?";
	}
}

// scheduler
namespace {

task* pick() {
	const int from = g_current ? (int)(g_current - g_tasks) : 0;
	for (int i = 1; i <= kMaxTask; ++i) {
		const int s = (from + i) % kMaxTask;
		if (g_tasks[s].state == kReady)
			return &g_tasks[s];
	}
	return nullptr;
}

uint32_t resume_esp(const task* t) { return (uint32_t)(uintptr_t)(t->kstack - kFrWords); }

void push_frame(task* t, const Registers& r) {
	uint32_t* f = t->kstack - kFrWords;
	memcpy(f, &r, sizeof(Registers));
}

extern "C" [[noreturn]] void task_switch_asm(uint32_t user_esp, uint32_t kernel_esp, uint32_t from_user,
											 uint32_t* slot);

void switch_to(task* to) {
	task* from = g_current;

	if (from && from->state == kRunning)
		from->state = kReady;
	to->state = kRunning;
	to->quantum = kQuantum;
	g_current = to;
	paging::space_switch(to->sp);
	const uint32_t from_user = (to->regs.cs & 3u) == 3u ? 1u : 0u;

	if (from_user)
		push_frame(to, to->regs);
	task_switch_asm(from_user ? resume_esp(to) : 0, to->kslot[5], from_user, to->kslot);
}

} // namespace

extern "C" void task_switch_now() {
	reap();
	task* to = pick();
	if (!to)
		return;
	switch_to(to);
}

extern "C" void task_suspend_asm(uint32_t* slot);

void schedule() {
	task_suspend_asm(g_current->kslot);
	// nothing else was runnable
}

void yield() { schedule(); }

void on_tick() {
	task* t = g_current;
	if (t)
		++t->utime;
}

bool preempt(Registers* r) {
	task* t = g_current;
	if (!t || t->pid == kPid1)
		return false;

	if ((r->cs & 3u) != 3u)
		return false;

	if (t->quantum && --t->quantum)
		return false;
	reap();
	task* to = pick();
	if (!to || to == t)
		return false;

	t->regs = *r;
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

bool kill(uint32_t pid, uint32_t sig) {
	task* t = find(pid);
	if (!t)
		return false;
	// SIGKILL cannot be caught
	if (t->state == kZombie || t->state == kKilled)
		return false;
	t->signal = sig;
	t->state = kKilled;
	if (t == g_current)
		exit(128 + (int)sig);

	return true;
}

void reap() {
	for (int i = 1; i < kMaxTask; ++i) {
		task* t = &g_tasks[i];
		if (t->state != kZombie || t == g_current)
			continue;

		task* parent = find(t->ppid);
		if (parent && parent->pid != t->pid && parent->done_n < (uint8_t)kMaxTask)
			parent->done_pid[parent->done_n++] = t->pid;
		destroy(t);
	}
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

[[noreturn]] void exit(int code) {
	task* t = g_current;
	if (t) {
		t->exit_code = (uint32_t)code;
		if (t->state != kKilled)
			t->signal = kSigNone;
		t->state = kZombie;
	}
	schedule();

	for (;;)
		asm volatile("hlt");
}

int fd_read_kernel(int fd, void* buf, uint32_t len) { return vfs::fd_read(fd, buf, len); }
int fd_write_kernel(int fd, const void* buf, uint32_t len) { return vfs::fd_write(fd, buf, len); }

} // namespace task
