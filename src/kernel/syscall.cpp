// the int 0x80 gate
#include "syscall.h"

#include "fs.h"
#include "lib/heap.h"
#include "lib/mem.h"
#include "lib/panic.h"
#include "lib/str.h"
#include "lib/time.h"
#include "task.h"
#include "vfs.h"

#include <stdint.h>

namespace {

// pushed by syscall_stub
struct regs {
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

extern "C" [[noreturn]] void exec_back(int code); // back 2 shell

int dispatch(regs* r) {
	switch (r->eax) {
	case SYS_read:
		return vfs::fd_read((int)r->ebx, (void*)r->ecx, r->edx);
	case SYS_write:
		return vfs::fd_write((int)r->ebx, (const void*)r->ecx, r->edx);
	case SYS_open:
		return vfs::fd_open((const char*)r->ebx, r->ecx);
	case SYS_close:
		return vfs::fd_close((int)r->ebx);
	case SYS_lseek:
		return vfs::lseek((int)r->ebx, (int)r->ecx, (int)r->edx);
	case SYS_readdir: {
		vfs::node* n = vfs::fd_node((int)r->ebx);
		if (!n)
			return -1;
		vfs::node out;
		if (vfs::readdir(n, r->ecx, &out) < 0)
			return -1;
		vfs_dirent* d = (vfs_dirent*)r->edx;
		size_t i = 0;
		while (out.name[i] && i < sizeof d->name - 1) {
			d->name[i] = out.name[i];
			++i;
		}
		d->name[i] = 0;
		d->inode = out.inode;
		d->size = out.size;
		d->type = out.type;
		return 0;
	}
	case SYS_exit:
		exec_back((int)r->ebx); // never returns
	case SYS_brk:
		return (int)task_brk(task::g_current, r->ebx);
	case SYS_sbrk:
		return (int)task_sbrk(task::g_current, (int)(int32_t)r->ebx);
	case SYS_writefile:
		return fs::write_file((const char*)r->ebx, (const void*)r->ecx, r->edx, r->esi) ? 0 : -1;
	case SYS_mkdir:
		return fs::mkdir((const char*)r->ebx) ? 0 : -1;
	case SYS_rm: {
		vfs::node* n = vfs::resolve((const char*)r->ebx);
		if (n && n->mount)
			return 1; // mount points stay
		return fs::rm((const char*)r->ebx) ? 0 : -1;
	}
	case SYS_fstat: {
		vfs::node* n = vfs::fd_node((int)r->ebx);
		if (!n)
			return -1;
		file_stat* s = (file_stat*)r->ecx;
		s->inode = n->inode;
		s->size = n->size;
		s->type = n->type;
		return 0;
	}
	case SYS_cwd: {
		const char* c = fs::cwd();
		const uint32_t n = (uint32_t)strlen(c);
		if (n >= r->ecx)
			return -1;
		memcpy((void*)r->ebx, c, n + 1);
		return 0;
	}
	case SYS_statfs: {
		uint32_t bs, blocks, free_b, inodes, free_i, compat, incompat, ro;
		fs::summary(&bs, &blocks, &free_b, &inodes, &free_i, &compat, &incompat, &ro);
		fs_stat* s = (fs_stat*)r->ebx;
		s->block_size = bs;
		s->blocks = blocks;
		s->free_blocks = free_b;
		s->inodes = inodes;
		s->free_inodes = free_i;
		s->feat_compat = compat;
		s->feat_incompat = incompat;
		s->feat_ro = ro;
		return 0;
	}
	case SYS_meminfo: {
		size_t total, used, free;
		heap_stats(&total, &used, &free);
		mem_stat* m = (mem_stat*)r->ebx;
		m->total = (uint32_t)total;
		m->used = (uint32_t)used;
		m->free = (uint32_t)free;
		return 0;
	}
	case SYS_uptime:
		if (!r->ebx)
			return -1;
		*(uint32_t*)r->ebx = time::ms();
		return 0;
	case SYS_panic:
		panic("app requested a panic");
	}
	return -1;
}

} // namespace

extern "C" uint32_t syscall_dispatch(regs* r) { return (uint32_t)dispatch(r); }