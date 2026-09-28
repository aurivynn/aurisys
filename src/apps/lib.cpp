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
int chdir(const char* path) { return (int)trp(SYS_chdir, (uint32_t)(uintptr_t)path, 0, 0, 0, 0); }
int ioctl(int fd, uint32_t req, void* arg) {
	return (int)trp(SYS_ioctl, (uint32_t)fd, req, (uint32_t)(uintptr_t)arg, 0, 0);
}

int tty_get(int fd, uint32_t* flags) { return ioctl(fd, kIoctlGetFlags, flags); }

int tty_set(int fd, uint32_t flags) { return ioctl(fd, kIoctlSetFlags, &flags); }

int tty_raw(int fd) { return tty_set(fd, 0); }

int tty_echo(int fd, int on) {
	uint32_t f = 0;
	if (tty_get(fd, &f) < 0)
		return -1;
	f = on ? (f | kTtyEcho) : (f & ~(uint32_t)kTtyEcho);
	return tty_set(fd, f);
}
int statfs(fs_stat* out) { return (int)trp(SYS_statfs, (uint32_t)(uintptr_t)out, 0, 0, 0, 0); }
int meminfo(mem_stat* out) { return (int)trp(SYS_meminfo, (uint32_t)(uintptr_t)out, 0, 0, 0, 0); }
int uptime_ms() {
	uint32_t ms = 0;
	(void)trp(SYS_uptime, (uint32_t)(uintptr_t)&ms, 0, 0, 0, 0);
	return (int)ms;
}

int fork() { return (int)trp(SYS_fork, 0, 0, 0, 0, 0); }

int execve(const char* path, char* const argv[], char* const envp[]) {
	return (int)trp(SYS_execve, (uint32_t)(uintptr_t)path, (uint32_t)(uintptr_t)argv, (uint32_t)(uintptr_t)envp, 0, 0);
}

int waitpid(int pid, int* status) {
	int code = 0;
	const int r = (int)trp(SYS_wait, (uint32_t)pid, (uint32_t)(uintptr_t)&code, 0, 0, 0);
	if (status)
		*status = (code >= 128 && code < 128 + 32) ? code : code << 8;
	return r;
}

int wait(int* status) { return waitpid(0, status); }

int kill(int pid, int sig) { return (int)trp(SYS_kill, (uint32_t)pid, (uint32_t)sig, 0, 0, 0); }

int getpid() { return (int)trp(SYS_getpid, 0, 0, 0, 0, 0); }
int getppid() { return (int)trp(SYS_getppid, 0, 0, 0, 0, 0); }
int dup(int fd) { return (int)trp(SYS_dup, (uint32_t)fd, 0, 0, 0, 0); }
int dup2(int fd, int nw) { return (int)trp(SYS_dup2, (uint32_t)fd, (uint32_t)nw, 0, 0, 0); }
int sleep_ms(uint32_t ms) { return (int)trp(SYS_sleep, ms, 0, 0, 0, 0); }

int sig_set(int sig, const sigaction* act, sigaction* old) {
	return (int)trp(SYS_sigaction, (uint32_t)sig, (uint32_t)(uintptr_t)act, (uint32_t)(uintptr_t)old, 0, 0);
}

char** environ = nullptr;

char* getenv(const char* name) {
	if (!environ || !name)
		return nullptr;
	const size_t n = strlen(name);
	for (char** e = environ; *e; ++e) {
		if (strncmp(*e, name, n) == 0 && (*e)[n] == '=')
			return *e + n + 1;
	}
	return nullptr;
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