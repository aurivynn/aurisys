#include "task.h"

#include "arch/gdt.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/str.h"

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
	for (int i = 1; i < kMaxTask; ++i)
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
	t->kstack = (uint32_t*)((uint32_t)t->kstack + kKernelStackBytes);
	t->pid = (uint32_t)(slot); // pid follows the slot until the table grows
	t->ppid = g_current ? g_current->pid : 0;
	t->state = kReady;
	strncpy(t->name, name ? name : "?", sizeof t->name - 1);
	// a child inherits the table it was created from, so the console ops come along.
	if (g_current) {
		memcpy(t->fd, g_current->fd, sizeof t->fd);
		t->nfd = (uint8_t)count_fds(t->fd);
	}
	(void)argv0;
	return t;
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
	g_tasks[0].sp = &paging::g_boot;
	strcpy(g_tasks[0].name, "init");
	return g_current;
}

// the kernel writes through pid 1, that is what makes fd 1 a file and not a global
int fd_open_kernel(const char* path, uint32_t flags) { return vfs::fd_open(path, flags); }
int fd_close_kernel(int fd) { return vfs::fd_close(fd); }
int fd_read_kernel(int fd, void* buf, uint32_t len) { return vfs::fd_read(fd, buf, len); }
int fd_write_kernel(int fd, const void* buf, uint32_t len) { return vfs::fd_write(fd, buf, len); }
vfs::node* fd_node_kernel(int fd) { return vfs::fd_node(fd); }

void describe(const task* t, char* out, uint32_t outsz) {
	if (!out || outsz == 0)
		return;
	if (!t || t->state == kFree) {
		out[0] = 0;
		return;
	}
	const char* st = "ready";
	switch (t->state) {
	case kRunning:
		st = "running";
		break;
	case kZombie:
		st = "zombie";
		break;
	case kKilled:
		st = "killed";
		break;
	default:
		break;
	}
	char tmp[96];
	uint32_t n = 0;
	const char* pre = "pid=";
	while (*pre && n + 1 < sizeof tmp)
		tmp[n++] = *pre++;
	for (uint32_t v = t->pid; v && n + 1 < sizeof tmp;) {
		char d[3];
		uint32_t k = 0;
		char rev[12];
		uint32_t r = 0;
		do {
			rev[r++] = (char)('0' + v % 10u);
			v /= 10u;
		} while (v && r < sizeof rev);
		while (r && n + 1 < sizeof tmp)
			tmp[n++] = rev[--r];
		(void)d;
		(void)k;
		break;
	}
	for (const char* p = " ppid="; *p && n + 1 < sizeof tmp;)
		tmp[n++] = *p++;
	for (uint32_t v = t->ppid; v && n + 1 < sizeof tmp;) {
		char rev[12];
		uint32_t r = 0;
		do {
			rev[r++] = (char)('0' + v % 10u);
			v /= 10u;
		} while (v && r < sizeof rev);
		while (r && n + 1 < sizeof tmp)
			tmp[n++] = rev[--r];
		break;
	}
	for (const char* p = " state="; *p && n + 1 < sizeof tmp;)
		tmp[n++] = *p++;
	for (const char* p = st; *p && n + 1 < sizeof tmp;)
		tmp[n++] = *p++;
	for (const char* p = " name="; *p && n + 1 < sizeof tmp;)
		tmp[n++] = *p++;
	for (const char* p = t->name; *p && n + 1 < sizeof tmp;)
		tmp[n++] = *p++;
	tmp[n] = 0;
	uint32_t c = 0;
	while (tmp[c] && c + 1 < outsz) {
		out[c] = tmp[c];
		++c;
	}
	out[c] = 0;
}

} // namespace task
