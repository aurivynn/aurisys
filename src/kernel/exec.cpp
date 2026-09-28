// loads a static elf and hands it to the scheduler
// apps are binaries in /bin
// only get reached through the kernel thru the int 0x80 gate
// they run on ring 3, traps switch to the tss stack
// a fresh process gets its own code, stack and arena

#include "exec.h"

#include "arch/gdt.h"
#include "arch/paging.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "shell/terminal.h"
#include "task.h"
#include "vfs.h"

#include <stdint.h>

namespace exec {

namespace {

using task::kAppBase;
using task::kArenaVA;
using task::kCodeBytes;
using task::kStackBase;
using task::kStackBytes;

constexpr uint32_t kArenaBase = kArenaVA; // where brk() grows, its own 2m slot

struct elfhdr {
	uint8_t ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint32_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct phdr {
	uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

constexpr uint32_t kPtLoad = 1;
constexpr uint16_t kEtExec = 2;
constexpr uint16_t kEm386 = 3;

bool elf_ok(const elfhdr& eh) {
	return eh.ident[0] == 0x7F && eh.ident[1] == 'E' && eh.ident[2] == 'L' && eh.ident[3] == 'F' && eh.ident[4] == 1 &&
		   eh.ident[5] == 1 && // 32 le
		   eh.type == kEtExec && eh.machine == kEm386;
}

// build the cdecl entry stack the apps crt0 expects: argc, then the argv pointers, then the strings themselves at the
// top
uint32_t build_stack(const char* flat) {
	const int arglen = ((const int*)flat)[0];
	const int envc = ((const int*)flat)[1];
	const char* p = flat + 8;

	int argc = 0;
	{
		const char* q = p;
		while ((int)(q - p) < arglen) {
			q += strlen(q) + 1;
			++argc;
		}
	}

	const uint32_t top = kStackBase + kStackBytes;
	uint32_t s = top;
	uint32_t ptrs[16];
	uint32_t eptrs[32];

	for (int i = 0; i < argc; ++i) {
		const size_t n = strlen(p);
		if (n + 1 > s - kStackBase)
			return 0;
		s -= (uint32_t)n + 1;
		memcpy((void*)s, p, n + 1);
		ptrs[i] = s;
		p += n + 1;
	}
	for (int i = 0; i < envc; ++i) {
		const size_t n = strlen(p);
		if (n + 1 > s - kStackBase)
			return 0;
		s -= (uint32_t)n + 1;
		memcpy((void*)s, p, n + 1);
		eptrs[i] = s;
		p += n + 1;
	}

	uint32_t sp = s;

	if (sp - kStackBase < 4u * (uint32_t)(argc + envc + 3) + 16u)
		return 0;
	sp -= 4; // the envp null
	*(uint32_t*)sp = 0;
	sp -= 4 * (uint32_t)envc; // the envp array
	uint32_t* ev = (uint32_t*)sp;
	for (int i = 0; i < envc; ++i)
		ev[i] = eptrs[i];
	sp -= 4; // the argv null
	*(uint32_t*)sp = 0;
	sp -= 4 * (uint32_t)argc; // the argv array
	uint32_t* av = (uint32_t*)sp;
	for (int i = 0; i < argc; ++i)
		av[i] = ptrs[i];
	sp -= 4; // argc
	*(uint32_t*)sp = (uint32_t)argc;
	return sp;
}

constexpr int kCopyArg = 16;
constexpr int kCopyEnv = 32;
constexpr int kCopyStr = 192;

static char g_flat[8 + (kCopyArg + kCopyEnv) * kCopyStr];

static bool copy_args(const char** av, int argc, const char** ev, int envc, char* out) {
	char* p = out + 8;
	for (int i = 0; i < argc; ++i) {
		const size_t n = strlen(av[i]);
		if (n >= kCopyStr)
			return false; // would be truncated
		memcpy(p, av[i], n + 1);
		p += n + 1;
	}

	const size_t arglen = (size_t)(p - (out + 8));
	for (int i = 0; i < envc; ++i) {
		const size_t n = strlen(ev[i]);
		if (n >= kCopyStr)
			return false;
		memcpy(p, ev[i], n + 1);
		p += n + 1;
	}
	((int*)out)[0] = (int)arglen;
	((int*)out)[1] = envc;
	return true;
}

// give the process its own view of the code, the stack and the arena
bool map_image(task::task* t) {
	for (uint32_t i = 0; i < kCodeBytes / 4096; ++i) {
		const uint32_t va = kAppBase + i * 4096;
		const uint32_t pa = (uint32_t)t->code_base + i * 4096;
		if (!paging::map_page(t->sp, va, pa, true))
			return false;
	}
	for (uint32_t i = 0; i < kStackBytes / 4096; ++i) {
		const uint32_t va = kStackBase + i * 4096;
		const uint32_t pa = (uint32_t)t->stack_base + i * 4096;
		if (!paging::map_page(t->sp, va, pa, true))
			return false;
	}
	// the arena, so brk has somewhere to hand out from
	for (uint32_t i = 0; i < task::kArenaBytes / 4096; ++i) {
		const uint32_t va = kArenaBase + i * 4096;
		const uint32_t pa = (uint32_t)t->heap_base + i * 4096;
		if (!paging::map_page(t->sp, va, pa, true))
			return false;
	}
	return true;
}

} // namespace

// read the elf, build the process, leave it runnable. returns its pid or 0
static bool load_into(task::task* t, const char* path, int argc, const char** argv, int envc, const char** envp,
					  Registers* out) {
	if (argc < 0 || argc > 16)
		return false;
	const int fd = vfs::fd_open(path, 0);
	if (fd < 0) {
		terminal::printf("%s: no such file\n", path);
		return false;
	}
	elfhdr eh;
	if (vfs::fd_read(fd, &eh, sizeof eh) != (int)sizeof eh || !elf_ok(eh)) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return false;
	}
	phdr ph[8];
	if (eh.phnum > 8 || eh.phentsize < 32) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return false;
	}
	vfs::lseek(fd, (int)eh.phoff, 0);
	const int want = (int)(eh.phnum * sizeof(phdr));
	if (vfs::fd_read(fd, ph, (uint32_t)want) != want) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return false;
	}
	if (!map_image(t)) {
		terminal::printf("%s: no room to map it\n", path);
		vfs::fd_close(fd);
		return false;
	}

	paging::space* here = task::g_current ? task::g_current->sp : &paging::g_boot;

	if (!copy_args(argv, argc, envp, envc, g_flat)) {
		paging::space_switch(here);
		vfs::fd_close(fd);
		return false;
	}
	paging::space_switch(t->sp);
	memset((void*)kAppBase, 0, kCodeBytes);
	// every segment, the bss tail is inside the memset above
	bool loaded = true;
	for (uint32_t i = 0; i < eh.phnum && loaded; ++i) {
		if (ph[i].type != kPtLoad)
			continue;
		if (ph[i].vaddr < kAppBase || ph[i].vaddr + ph[i].memsz > kAppBase + kCodeBytes) {
			terminal::printf("%s: program too big\n", path);
			loaded = false;
			break;
		}
		vfs::lseek(fd, (int)ph[i].offset, 0);
		if (vfs::fd_read(fd, (void*)ph[i].vaddr, ph[i].filesz) != (int)ph[i].filesz) {
			terminal::printf("%s: short read\n", path);
			loaded = false;
		}
	}
	const uint32_t esp = loaded ? build_stack(g_flat) : 0;
	if (loaded && esp == 0) {
		terminal::printf("%s: arg list too big\n", path);
		loaded = false;
	}
	paging::space_switch(here);
	vfs::fd_close(fd);
	if (!loaded)
		return false;

	Registers r = {};
	r.eip = eh.entry;
	r.cs = kSelUserCode;
	r.eflags = 0x202; // if set, so irqs still tick during apps, plus the reserved bit
	r.user_esp = esp;
	r.user_ss = kSelUserData;
	r.esp_dummy = esp; // the slot pushad drops, keep it pointing somewhere sane
	*out = r;
	return true;
}

uint32_t spawn(const char* path, int argc, const char** argv) {
	return spawn_mapped(path, argc, argv, nullptr, 0, 0, nullptr);
}

uint32_t spawn_mapped(const char* path, int argc, const char** argv, const fdmap* map, int nmap, int envc,
					  const char** envp) {
	task::task* t = task::fork();
	if (!t) {
		terminal::printf("%s: no room for a process\n", path);
		return 0;
	}
	strncpy(t->name, path, sizeof t->name - 1);
	t->name[sizeof t->name - 1] = 0;

	Registers r = {};
	if (!load_into(t, path, argc, argv, envc, envp, &r)) {
		task::destroy(t);
		return 0;
	}
	t->regs = r;

	for (int i = 0; i < nmap; ++i)
		vfs::fd_install(t, map[i].child_fd, map[i].from);
	// it has an image and an entry point now, so the scheduler may have it
	task::arm(t);
	return t->pid;
}

bool replace(const char* path, int argc, const char** argv, int envc, const char** envp) {
	task::task* t = task::g_current;
	if (!t || !t->sp)
		return false;

	const bool shared = !t->owns_frames;
	paging::space* old = t->sp;
	uint32_t* old_code = t->code_base;
	uint32_t* old_stack = t->stack_base;
	uint32_t* old_heap = t->heap_base;
	uint32_t* old_end = t->heap_end;
	uint32_t* old_brk = t->brk;

	paging::space* fresh = paging::space_create();
	if (!fresh)
		return false;
	t->sp = fresh;
	if (!give_frames(t)) {
		paging::space_destroy(fresh);
		t->sp = old;
		t->code_base = old_code;
		t->stack_base = old_stack;
		t->heap_base = old_heap;
		t->heap_end = old_end;
		t->brk = old_brk;
		return false;
	}

	t->owns_frames = true;

	Registers r = {};
	if (!load_into(t, path, argc, argv, envc, envp, &r)) {
		kframe_free(t->code_base);
		kframe_free(t->stack_base);
		kframe_free(t->heap_base);
		paging::space_destroy(fresh);
		t->sp = old;
		t->code_base = old_code;
		t->stack_base = old_stack;
		t->heap_base = old_heap;
		t->heap_end = old_end;
		t->brk = old_brk;
		return false;
	}

	paging::space_switch(t->sp);
	paging::space_destroy(old);
	if (!shared) {
		kframe_free(old_heap);
		kframe_free(old_code);
		kframe_free(old_stack);
	}

	t->regs = r;
	strncpy(t->name, path, sizeof t->name - 1);
	t->name[sizeof t->name - 1] = 0;

	memset(t->sig_handlers, 0, sizeof t->sig_handlers);
	t->sig_mask = 0;
	t->sig_pending = 0;
	t->sig_active = 0;

	task::resume_after_exec();
}

bool run(const char* path, int argc, const char** argv, uint32_t* exit_code) {
	return run_mapped(path, argc, argv, nullptr, 0, exit_code);
}

bool run_mapped(const char* path, int argc, const char** argv, const fdmap* map, int nmap, uint32_t* exit_code) {
	const int envc = terminal::env_count();
	const char** envp = terminal::env_vector();
	const uint32_t pid = spawn_mapped(path, argc, argv, map, nmap, envc, envp);
	if (pid == 0)
		return false;

	task::set_foreground(task::find(pid));

	task::task* t = nullptr;
	for (;;) {
		t = task::find(pid);
		if (!t || t->state == task::kZombie)
			break;
		task::yield();
	}

	task::clear_foreground(t);

	const uint32_t code = t ? t->exit_code : 0;
	const uint32_t sig = t ? t->signal : 0;
	task::reap();

	task::take_signal(pid);
	if (exit_code)
		*exit_code = sig ? 128u + sig : code;
	return true;
}

} // namespace exec