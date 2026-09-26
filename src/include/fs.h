#pragma once

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

bool mount(uint32_t disk_lba);				  // mbr -> partition -> superblock. false = no fs
bool lookup(const char* path, uint32_t* ino); // any depth. relative resolves against cwd
bool getstat(uint32_t ino, struct stat* out);
uint32_t read(uint32_t ino, void* buf, uint32_t len, uint32_t off); // bytes read 0 = eof/err
int list_dir(uint32_t dir_ino, dirent_cb cb, void* ctx);			// entries seen -1 error 0 empty

const char* cwd();
bool chdir(const char* path);							   // resolves relative to cwd, must be a dir
bool resolve(const char* path, char* buf, uint32_t bufsz); // absolute form, "/a/../b" -> "/b"

void summary(uint32_t* block_size, uint32_t* blocks, uint32_t* free_blocks, uint32_t* inodes, uint32_t* free_inodes,
			 uint32_t* feat_compat, uint32_t* feat_incompat, uint32_t* feat_ro);

bool write_file(const char* path, const void* data, uint32_t len);

} // namespace fs