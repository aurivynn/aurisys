// loads a static elf and hands it to the scheduler
// apps are binaries in /bin
// only get reached through the kernel thru the int 0x80 gate
// they run on ring 3, traps switch to the tss stack
// a fresh process gets its own code, stack and arena

#include "exec.h"

#include "arch/gdt.h"
#include "arch/paging.h"
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
uint32_t build_stack(int argc, const char** argv) {
	const uint32_t top = kStackBase + kStackBytes;
	uint32_t s = top;
	uint32_t ptrs[16];
	for (int i = 0; i < argc; ++i) {
		const uint32_t len = (uint32_t)strlen(argv[i]);
		if (len + 1 > s - kStackBase)
			return 0;
		s -= len + 1;
		memcpy((void*)s, argv[i], len + 1);
		ptrs[i] = s;
	}
	uint32_t sp = s;
	if (sp - kStackBase < 4u * (uint32_t)(argc + 2) + 16u)
		return 0;
	sp -= 8; // a null pair, nothing else set yet
	((uint32_t*)sp)[0] = 0;
	((uint32_t*)sp)[1] = 0;
	sp -= 4; // empty
	*(uint32_t*)sp = 0;
	sp -= 4 * (uint32_t)(argc + 1); // the argv array
	uint32_t* av = (uint32_t*)sp;
	for (int i = 0; i < argc; ++i)
		av[i] = ptrs[i];
	av[argc] = 0;
	sp -= 4; // argc
	*(uint32_t*)sp = (uint32_t)argc;
	return sp;
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
uint32_t spawn(const char* path, int argc, const char** argv) {
	if (argc < 0 || argc > 16)
		return 0;
	const int fd = vfs::fd_open(path, 0);
	if (fd < 0) {
		terminal::printf("%s: no such file\n", path);
		return 0;
	}
	elfhdr eh;
	if (vfs::fd_read(fd, &eh, sizeof eh) != (int)sizeof eh || !elf_ok(eh)) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return 0;
	}
	phdr ph[8];
	if (eh.phnum > 8 || eh.phentsize < 32) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return 0;
	}
	vfs::lseek(fd, (int)eh.phoff, 0);
	const int want = (int)(eh.phnum * sizeof(phdr));
	if (vfs::fd_read(fd, ph, (uint32_t)want) != want) {
		terminal::printf("%s: not an executable\n", path);
		vfs::fd_close(fd);
		return 0;
	}

	task::task* t = task::fork();
	if (!t) {
		terminal::printf("%s: no room for a process\n", path);
		vfs::fd_close(fd);
		return 0;
	}
	strncpy(t->name, path, sizeof t->name - 1);
	t->name[sizeof t->name - 1] = 0;
	if (!map_image(t)) {
		terminal::printf("%s: no room to map it\n", path);
		task::destroy(t);
		vfs::fd_close(fd);
		return 0;
	}

	paging::space* here = task::g_current ? task::g_current->sp : &paging::g_boot;
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
	const uint32_t esp = loaded ? build_stack(argc, argv) : 0;
	if (loaded && esp == 0) {
		terminal::printf("%s: arg list too big\n", path);
		loaded = false;
	}
	paging::space_switch(here);
	vfs::fd_close(fd);
	if (!loaded) {
		task::destroy(t);
		return 0;
	}

	Registers r = {};
	r.eip = eh.entry;
	r.cs = kSelUserCode;
	r.eflags = 0x202; // if set, so irqs keep ticking, plus the reserved bit
	r.user_esp = esp;
	r.user_ss = kSelUserData;
	r.esp_dummy = esp; // the slot pushad drops, keep it pointing somewhere sane
	t->regs = r;
	return t->pid;
}

bool run(const char* path, int argc, const char** argv, uint32_t* exit_code) {
	const uint32_t pid = spawn(path, argc, argv);
	if (pid == 0)
		return false;

	task::task* t = nullptr;
	for (;;) {
		t = task::find(pid);
		if (!t || t->state == task::kZombie)
			break;
		task::yield();
	}

	const uint32_t code = t ? t->exit_code : 0;
	const uint32_t sig = t ? t->signal : 0;
	task::reap();

	task::take_signal(pid);
	if (exit_code)
		*exit_code = sig ? 128u + sig : code;
	return true;
}

} // namespace exec