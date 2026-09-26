// loads static elf and runs
// apps are binaries in /bin
// only get reached through kernel throu int 0x80 gate
// they run on the current ring rn
// given fresh stack with fixed va

#include "exec.h"

#include "arch/paging.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "shell/terminal.h"
#include "vfs.h"

#include <stdint.h>

extern "C" void jump_to_app(uint32_t entry, uint32_t esp);

namespace exec {

namespace {

constexpr uint32_t kAppBase = 0x40000000;	// fixed link va
constexpr uint32_t kCodeBytes = 0x40000;	// the code and data arena
constexpr uint32_t kStackBase = 0x40100000; // a fresh stack for the app
constexpr uint32_t kStackBytes = 0x10000;

uint8_t g_code[kCodeBytes] __attribute__((section(".paging"), aligned(4096)));
uint8_t g_stack[kStackBytes] __attribute__((section(".paging"), aligned(4096)));

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

} // namespace

bool run(const char* path, int argc, const char** argv) {
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
	// a fresh page table for the arena
	paging::app_unmap();
	for (uint32_t i = 0; i < kCodeBytes / 4096; ++i)
		paging::map_app(kAppBase + i * 4096, (uint32_t)(uintptr_t)&g_code[i * 4096]);
	for (uint32_t i = 0; i < kStackBytes / 4096; ++i)
		paging::map_app(kStackBase + i * 4096, (uint32_t)(uintptr_t)&g_stack[i * 4096]);
	memset((void*)kAppBase, 0, kCodeBytes);
	// load every segment as the bss tail is inside the memset above
	for (uint32_t i = 0; i < eh.phnum; ++i) {
		if (ph[i].type != kPtLoad)
			continue;
		if (ph[i].vaddr < kAppBase || ph[i].vaddr + ph[i].memsz > kAppBase + kCodeBytes) {
			terminal::printf("%s: program too big\n", path);
			vfs::fd_close(fd);
			return false;
		}
		vfs::lseek(fd, (int)ph[i].offset, 0);
		if (vfs::fd_read(fd, (void*)ph[i].vaddr, ph[i].filesz) != (int)ph[i].filesz) {
			terminal::printf("%s: short read\n", path);
			vfs::fd_close(fd);
			return false;
		}
	}
	vfs::fd_close(fd);
	const uint32_t esp = build_stack(argc, argv);
	if (esp == 0) {
		terminal::printf("%s: arg list too big\n", path);
		return false;
	}
	paging::app_flush();
	jump_to_app(eh.entry, esp);
	asm volatile("sti");
	paging::app_unmap();
	return true;
}

} // namespace exec