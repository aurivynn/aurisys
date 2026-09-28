// the int 0x80 gate
#include "syscall.h"

#include "exec.h"
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
		task::exit((int)r->ebx);
	case SYS_brk:
		return (int)task_brk(task::g_current, r->ebx);
	case SYS_sbrk:
		return (int)task_sbrk(task::g_current, (int)(int32_t)r->ebx);
	case SYS_mkdir:
		return fs::mkdir(task::cwd(), (const char*)r->ebx) ? 0 : -1;
	case SYS_unlink: {
		vfs::node* n = vfs::resolve((const char*)r->ebx);
		if (n && n->mount)
			return 1; // mount points stay
		return fs::rm(task::cwd(), (const char*)r->ebx) ? 0 : -1;
	}
	case SYS_pipe: {
		int fds[2] = {-1, -1};
		if (vfs::pipe(fds))
			return -1;
		int* out = (int*)r->ebx;
		out[0] = fds[0];
		out[1] = fds[1];
		return 0;
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
		const char* c = task::cwd();
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

	// processes
	case SYS_fork: {
		const uint32_t pid = task::fork_user((const Registers*)r);
		return (int)pid;
	}
	case SYS_execve: {
		const char* path = (const char*)r->ebx;
		const char** av = (const char**)r->ecx;
		const char* argbuf[17];
		int argc = 0;
		if (av) {
			while (argc < 16 && av[argc]) {
				argbuf[argc] = av[argc];
				++argc;
			}
		}
		argbuf[argc] = nullptr;
		if (!path)
			return -kErrInval;

		if (!exec::replace(path, argc, argbuf))
			return -kErrNoEnt;

		return 0;
	}
	case SYS_wait: {
		uint32_t code = 0;
		const int32_t pid = task::wait_for((int32_t)r->ebx, &code);

		if (pid > 0)
			*(uint32_t*)r->ecx = code;
		return (int)pid;
	}
	case SYS_kill: {
		if (r->ecx >= (uint32_t)kSigMax)
			return -kErrInval;
		return task::kill(r->ebx, r->ecx) ? 0 : -kErrSrch;
	}
	case SYS_getpid:
		return task::g_current ? (int)task::g_current->pid : -kErrInval;
	case SYS_getppid:
		return task::g_current ? (int)task::g_current->ppid : -kErrInval;
	case SYS_dup:
		return vfs::dup2((int)r->ebx, vfs::next_free());
	case SYS_dup2: {
		const int nw = vfs::dup2((int)r->ebx, (int)r->ecx);
		return nw < 0 ? -kErrBadf : nw;
	}
	case SYS_sleep: {
		const uint32_t ms = r->ebx;
		const uint32_t ticks = (ms + 9u) / 10u;
		return task::sleep_ticks(ticks);
	}
	case SYS_sigaction: {
		const uint32_t sig = r->ebx;
		if (sig == 0 || sig >= (uint32_t)kSigMax || !task::g_current)
			return -kErrInval;
		const sigaction* in = (const sigaction*)r->ecx;
		sigaction* old = (sigaction*)r->edx;
		task::task* t = task::g_current;
		if (old) {
			old->handler = t->sig_handlers[sig];
			old->mask = t->sig_masks[sig];
			old->flags = 0;
		}
		if (in) {
			// SIGKILL and SIGSTOP cannot be caught
			if (sig == kSigKill)
				return -kErrInval;
			t->sig_handlers[sig] = in->handler;
			t->sig_masks[sig] = in->mask;
		}
		return 0;
	}
	case SYS_sigreturn:
		task::sigreturn((Registers*)r);
		return 0;
	}
	return -kErrInval;
}

} // namespace

extern "C" uint32_t syscall_dispatch(regs* r) {
	const uint32_t out = (uint32_t)dispatch(r);
	task::deliver_pending(task::g_current, (Registers*)r);
	return out;
}