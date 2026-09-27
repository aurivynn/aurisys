#pragma once

#include "syscall.h"

#include <stdint.h>

namespace fs {

struct stat {
	uint32_t ino;
	uint32_t size;	 // bytes
	uint16_t mode;	 // type bits: dir 0x4000, regular 0x8000
	uint32_t blocks; // in 512byte units
	uint16_t links;
};

using dirent_cb = bool (*)(const char* name, uint32_t ino, uint8_t type, void* ctx);

bool mount(uint32_t disk_lba); // mbr -> partition -> superblock. false = no fs
bool getstat(uint32_t ino, struct stat* out);
uint32_t read(uint32_t ino, void* buf, uint32_t len, uint32_t off); // bytes read 0 = eof/err
int list_dir(uint32_t dir_ino, dirent_cb cb, void* ctx);			// entries seen -1 error 0 empty

bool resolve(const char* path, const char* base, char* buf, uint32_t bufsz); // "/a/../b" -> "/b"
bool chdir(const char* base, const char* path, char* out, uint32_t outsz);	 // must land on a dir

void summary(uint32_t* block_size, uint32_t* blocks, uint32_t* free_blocks, uint32_t* inodes, uint32_t* free_inodes,
			 uint32_t* feat_compat, uint32_t* feat_incompat, uint32_t* feat_ro);

bool write_file(const char* base, const char* path, const void* data, uint32_t len, uint32_t flags);
// write into an open file at an offset
bool write_at(uint32_t ino, const void* buf, uint32_t off, uint32_t len);
bool mkdir(const char* base, const char* path);
bool rm(const char* base, const char* path);

} // namespace fs