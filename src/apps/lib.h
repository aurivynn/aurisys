#pragma once

// the userspace lib

#include "syscall.h"

#include "lib/mem.h"
#include "lib/print.h"
#include "lib/str.h"

#include <stdint.h>

enum { kSeekSet = 0, kSeekCur = 1, kSeekEnd = 2 };

int open(const char* path, uint32_t flags);
int read(int fd, void* buf, uint32_t len);
int write(int fd, const void* buf, uint32_t len);
int close(int fd);
int lseek(int fd, int off, int whence);
int readdir(int fd, uint32_t index, vfs_dirent* out);
int fstat(int fd, file_stat* out);
int writefile(const char* path, const void* buf, uint32_t len, uint32_t flags);
int mkdir(const char* path);
int rm(const char* path);
int getcwd(char* buf, uint32_t size);
int statfs(fs_stat* out);
int meminfo(mem_stat* out);
int uptime_ms();

int brk(uint32_t addr);
int sbrk(int delta);
int printf(const char* fmt, ...);
bool is_dot(const char* n);

extern "C" void exit(int code);
extern "C" int main(int argc, char** argv);