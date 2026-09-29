#pragma once

#include "arch/isr.h"
#include "arch/paging.h"
#include "syscall.h"
#include "vfs.h"

#include <stdint.h>

// processes as address spaces, not as entries in a table
namespace task {

enum : uint32_t {
	kFree = 0,
	kReady,	  // runnable, waiting for a turn
	kRunning, // on a cpu
	kZombie,  // exited, still in the table waiting to be reaped
	kKilled,  // took a signal, unwinding
	kBlocked, // waiting on a device, off the run queue
};

constexpr int kMaxTask = 8;
constexpr int kPid1 = 1; // the shell, the kernel writes through its fds

// how many pit ticks a process gets before it is moved along
constexpr uint32_t kQuantum = 2;

// signals
enum : uint32_t {
	kSigChild = 17, // a child changed state
	kSigKill = 9,	// cannot be caught
	kSigSegv = 11,	// touched something it should not have
	kSigTerm = 15,	// asked to stop
	kSigInt = 2,	// ctrl c
	kSigNone = 0,
};

const char* signal_name(uint32_t sig);
const char* state_name(uint32_t state);

struct task {
	uint32_t pid, ppid;
	uint32_t state;
	uint32_t exit_code;
	uint32_t signal;   // what killed it, kSigNone if it just exited
	paging::space* sp; // address space root, cr3 for this process
	uint32_t* kstack;  // kernel stack top
	Registers regs;	   // save area, filled in when we leave the process
	vfs::ofile fd[vfs::kMaxFd];
	uint8_t nfd; // how many fds are open
	char name[16];

	uint32_t* heap_base; // first byte the process may use
	uint32_t* heap_end;	 // one past the last, the ceiling
	uint32_t* brk;		 // the break where the next brk() hands out from

	// the process image
	uint32_t* code_base;
	uint32_t code_bytes;

	// the user stack
	uint32_t* stack_base;

	// where this process is
	char cwd[128];

	// where to pick this process up again
	uint32_t kslot[8]; // ebx esi edi ebp, return address, entry esp, eax, ecx
	uint32_t quantum;  // ticks left before it is moved along
	uint32_t utime;	   // ticks it has actually had, for /proc
	uint32_t ktime;	   // ticks it spent in the kernel, for /proc

	// children that have ended and not been collected yet, oldest first
	uint32_t done_pid[kMaxTask];
	uint32_t done_code[kMaxTask]; // how it ended
	uint8_t done_n;				  // how many entries of done_pid are live
	bool waiting;				  // parked in wait() so reap knows to wake it
	int wait_pid;				  // the pid it asked for, or 0 for any child

	uint8_t wake_pending;

	// signals
	uint32_t sig_handlers[kSigMax]; // user addresses 0 for the default action
	uint32_t sig_masks[kSigMax];	// what each handler asked to hold off
	uint32_t sig_mask;				// blocked right now
	uint32_t sig_pending;			// one waiting enough for a queue of one
	uint32_t sig_active;			// the signal whose handler is running
	uint32_t sig_saved_mask;		// the mask to put back when it returns
	uint32_t sleep_until;			// tick it is asleep until 0 if awake

	bool in_syscall;
	bool owns_frames;
	bool armed;
};

constexpr uint32_t kArenaBytes = 1024u * 1024u;
constexpr uint32_t kArenaVA = 0x40200000u;

constexpr uint32_t kCodeBytes = 0x40000u;
constexpr uint32_t kAppBase = 0x40000000u;
constexpr uint32_t kStackBase = 0x40100000u;
constexpr uint32_t kStackBytes = 0x10000u;

uint32_t task_brk(task* t, uint32_t addr);
uint32_t task_sbrk(task* t, int delta);

// the process table. slot 0 is never used so a null pid is impossible
extern task g_tasks[kMaxTask];
extern task* g_current;

task* create(const char* name, const char* argv0); // fresh space, fresh fds

void arm(task* t);
bool give_frames(task* t);
void destroy(task* t);
task* find(uint32_t pid);
task* by_pid(uint32_t pid);
task* init(); // pid 1, the shell

// scheduler

void schedule(); // give up the cpu, come back when picked again
void yield();	 // same thing, spelled for callers that block

// give the machine to the first process
[[noreturn]] void become_first();

// off the run queue until somebody unblocks us, and back on it when they do
void block();
void block_on(task** slot);
void unblock(task* t);
uint32_t ticks_now();		 // timer interrupts since boot
int sleep_ticks(uint32_t n); // park for n ticks, or until a signal arrives
void on_tick();				 // the pit handler calls this
bool preempt(Registers* r);	 // true if the tick took the cpu away
int runnable();				 // how many are ready, for the boot test

// fork the current process
task* fork();

uint32_t fork_user(const Registers* frame);

// end the current process
[[noreturn]] void exit(int code);
[[noreturn]] void resume_after_exec();

// signals
bool kill(uint32_t pid, uint32_t sig);
// like kill, but never ends the process on the spot, so it is safe to call from a tick. see the comment on it in
// task.cpp.
bool signal_task(uint32_t pid, uint32_t sig);
void reap(); // collect the zombies, tell the parents
int32_t wait_for(int32_t pid, uint32_t* code, bool nowait);
void deliver_pending(task* t, Registers* r);
void sigreturn(Registers* r);
uint32_t take_signal(uint32_t for_pid);
int alive(); // processes that are not free, the shell included

// the current process working directory. "/" before the root is mounted
const char* cwd();
bool chdir(const char* path); // false unless it lands on a directory

// the fd table the kernel itself uses, aka what printf(1, ...) hits
int fd_read_kernel(int fd, void* buf, uint32_t len);
int fd_write_kernel(int fd, const void* buf, uint32_t len);

} // namespace task
