#pragma once

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
	SYS_writefile,
	SYS_mkdir,
	SYS_rm,
	SYS_fstat,
	SYS_cwd,
	SYS_statfs,
	SYS_meminfo,
	SYS_uptime,
	SYS_panic,
};

enum { kTypeFile = 1, kTypeDir = 2, kTypeChar = 3 };

// write modes, values match the fs side
enum { kWriteTrunc = 1, kWriteAppend = 2 };

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