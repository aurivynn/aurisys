#pragma once

#include <stddef.h>
#include <stdint.h>

// app/kernel contract
// apps are freestanding and reach the kernel through int 0x80
// eax = numb and ebx etc aas args
// result comeds back as eax and apps share this header like a uapi

enum {
	SYS_read = 1,
	SYS_write,
	SYS_open,
	SYS_close,
	SYS_lseek,
	SYS_readdir,
	SYS_exit,
	SYS_brk,  // move the process break
	SYS_sbrk, // move the break by a delta
	SYS_mkdir,
	SYS_unlink,
	SYS_fstat,
	SYS_cwd,
	SYS_statfs,
	SYS_meminfo,
	SYS_uptime,
	SYS_panic,
	SYS_pipe,

	SYS_fork,
	SYS_execve,
	SYS_wait,
	SYS_kill,
	SYS_getpid,
	SYS_getppid,
	SYS_dup,
	SYS_dup2,
	SYS_sleep,
	SYS_sigaction,
	SYS_sigreturn,
};

// errno
enum {
	kErrInval = 22, // something about the arguments was wrong
	kErrNoEnt = 2,	// no such file, process or directory
	kErrPerm = 1,	// refused, and not because it does not exist
	kErrBadf = 9,	// that descriptor is not open
	kErrAgain = 11, // would block, try again
	kErrNoMem = 12, // out of room
	kErrExist = 17, // it is already there
	kErrChild = 10, // no such child to wait for
	kErrSrch = 3,	// no such process
	kErrPipe = 29,	// the pipe is gone
	kErrRange = 34, // the number is past the end of what fits
	kErr2Big = 7,	// the stack could not hold it
	kErrIntr = 4,	// interrupted before it got anywhere
};

enum { kTypeFile = 1, kTypeDir = 2, kTypeChar = 3 };

constexpr uint32_t O_RDONLY = 0u; // opening for reading is the absence of bits
constexpr uint32_t O_WRONLY = 1u;
constexpr uint32_t O_RDWR = 2u;
constexpr uint32_t O_CREAT = 0100u;	  // 64
constexpr uint32_t O_TRUNC = 01000u;  // 512
constexpr uint32_t O_APPEND = 02000u; // 1024

struct vfs_dirent {
	char name[64];
	uint32_t inode;
	uint32_t size; // bytes
	uint8_t type;  // one of the kType bits
};

struct file_stat {
	uint32_t inode;
	uint32_t size;
	uint8_t type;
};

struct fs_stat {
	uint32_t block_size;
	uint32_t blocks, free_blocks;
	uint32_t inodes, free_inodes;
	uint32_t feat_compat, feat_incompat, feat_ro;
};

struct mem_stat {
	uint32_t total, used, free;
};

// signals

enum {
	kSigInt = 2,  // ctrl c
	kSigKill = 9, // cannot be caught or ignored
	kSigUsr1 = 10,
	kSigSegv = 11, // bad address
	kSigUsr2 = 12,
	kSigPipe = 13, // a write to a pipe with no reader
	kSigAlarm = 14,
	kSigTerm = 15,
	kSigChild = 17, // a child changed state
	kSigCont = 18,
	kSigStop = 19,
};

constexpr int kSigMax = 32; // the handler table is this wide

// sigaction(sig, act, old). handler is a user address or 0 to put the signal back to its default.
// mask lists the signals to hold off while the handler runs, as a bitmask over kSigInt..
struct sigaction {
	uint32_t handler;
	uint32_t flags;
	uint32_t mask;
};

struct sigframe {
	uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; // the eight as pushed
	uint32_t int_no, err_code;
	uint32_t eip, cs, eflags;
	uint32_t user_esp, user_ss;
	uint32_t sig;	   // which signal this was
	uint32_t fp;	   // address of this frame so a handler can read the above
	uint32_t ret;	   // where the handler returns to the trampoline
	uint32_t arg0;	   // handler(sig, fp): sig
	uint32_t arg1;	   // and the frame pointer
	uint32_t tramp[3]; // mov eax, SYS_sigreturn / int 0x80 / ud2
};

static_assert(offsetof(sigframe, arg0) == offsetof(sigframe, ret) + sizeof(uint32_t),
			  "the two arguments follow the return address");
static_assert(offsetof(sigframe, tramp) >= offsetof(sigframe, arg1) + sizeof(uint32_t),
			  "the trampoline must not sit underneath the arguments");
static_assert(sizeof(sigframe) >= offsetof(sigframe, tramp) + 9,
			  "the trampoline is nine bytes and has to fit in the frame");
static_assert(sizeof(sigframe) == 92, "the frame is what the offsets above add up to");