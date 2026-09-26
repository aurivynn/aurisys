#pragma once

#include "arch/isr.h"
#include "arch/paging.h"
#include "vfs.h"

#include <stdint.h>

// processes as address spaces, not as entries in a table
namespace task {

enum : uint32_t {
	kFree = 0,
	kReady,	  // runnable, waiting for a turn
	kRunning, // on a cpu
	kZombie,  // exited, waiting to be Bye
	kKilled,  // took a signal, unwinding
};

constexpr int kMaxTask = 8;
constexpr int kPid1 = 1; // the shell, the kernel writes through its fds

struct task {
	uint32_t pid, ppid;
	uint32_t state;
	uint32_t exit_code;
	uint32_t signal;   // pending, delivered at the next return to user mode
	paging::space* sp; // address space root, cr3 for this process
	uint32_t* kstack;  // kernel stack top
	Registers regs;	   // save area, filled in when we leave the process
	vfs::ofile fd[vfs::kMaxFd];
	uint8_t nfd; // how many fds are open
	char name[16];
};

// the process table. slot 0 is never used so a null pid is impossible
extern task g_tasks[kMaxTask];
extern task* g_current;

task* create(const char* name, const char* argv0); // fresh space, fresh fds
void destroy(task* t);
task* find(uint32_t pid);
task* by_pid(uint32_t pid);
task* init(); // pid 1, the shell

// the fd table the kernel itself uses, ie what printf(1, ...) hits
int fd_open_kernel(const char* path, uint32_t flags);
int fd_close_kernel(int fd);
int fd_read_kernel(int fd, void* buf, uint32_t len);
int fd_write_kernel(int fd, const void* buf, uint32_t len);
vfs::node* fd_node_kernel(int fd);

// print a task the way ps will, used by the fault path and /proc
void describe(const task* t, char* out, uint32_t outsz);

} // namespace task
