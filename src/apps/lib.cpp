// the userspace runtime

#include "lib.h"

#include <stdarg.h>
#include <stdint.h>

static uint32_t trp(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
	uint32_t out;
	asm volatile("int $0x80" : "=a"(out) : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
	return out;
}

int open(const char* path, uint32_t flags) { return (int)trp(SYS_open, (uint32_t)(uintptr_t)path, flags, 0, 0, 0); }
int read(int fd, void* buf, uint32_t len) {
	return (int)trp(SYS_read, (uint32_t)fd, (uint32_t)(uintptr_t)buf, len, 0, 0);
}
int write(int fd, const void* buf, uint32_t len) {
	return (int)trp(SYS_write, (uint32_t)fd, (uint32_t)(uintptr_t)buf, len, 0, 0);
}
int close(int fd) { return (int)trp(SYS_close, (uint32_t)fd, 0, 0, 0, 0); }
int lseek(int fd, int off, int whence) {
	return (int)trp(SYS_lseek, (uint32_t)fd, (uint32_t)off, (uint32_t)whence, 0, 0);
}
int readdir(int fd, uint32_t index, vfs_dirent* out) {
	return (int)trp(SYS_readdir, (uint32_t)fd, index, (uint32_t)(uintptr_t)out, 0, 0);
}
int fstat(int fd, file_stat* out) { return (int)trp(SYS_fstat, (uint32_t)fd, (uint32_t)(uintptr_t)out, 0, 0, 0); }
int mkdir(const char* path) { return (int)trp(SYS_mkdir, (uint32_t)(uintptr_t)path, 0, 0, 0, 0); }
int unlink(const char* path) { return (int)trp(SYS_unlink, (uint32_t)(uintptr_t)path, 0, 0, 0, 0); }
int pipe(int fds[2]) { return (int)trp(SYS_pipe, (uint32_t)(uintptr_t)fds, 0, 0, 0, 0); }
int getcwd(char* buf, uint32_t size) { return (int)trp(SYS_cwd, (uint32_t)(uintptr_t)buf, size, 0, 0, 0); }
int statfs(fs_stat* out) { return (int)trp(SYS_statfs, (uint32_t)(uintptr_t)out, 0, 0, 0, 0); }
int meminfo(mem_stat* out) { return (int)trp(SYS_meminfo, (uint32_t)(uintptr_t)out, 0, 0, 0, 0); }
int uptime_ms() {
	uint32_t ms = 0;
	(void)trp(SYS_uptime, (uint32_t)(uintptr_t)&ms, 0, 0, 0, 0);
	return (int)ms;
}

int brk(uint32_t addr) { return (int)(uint32_t)trp(SYS_brk, addr, 0, 0, 0, 0); }

int sbrk(int delta) { return (int)(uint32_t)trp(SYS_sbrk, (uint32_t)(int32_t)delta, 0, 0, 0, 0); }

bool is_dot(const char* n) { return n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)); }

namespace {

void putc1(char c) {
	const char b = c;
	(void)trp(SYS_write, 1, (uint32_t)(uintptr_t)&b, 1, 0, 0);
}

} // namespace

int printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(putc1, fmt, ap);
	va_end(ap);
	return 0;
}

extern "C" void exit(int code) {
	(void)trp(SYS_exit, (uint32_t)code, 0, 0, 0, 0);
	for (;;) {
	}
}