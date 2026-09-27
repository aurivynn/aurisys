#include "lib.h"

static const char* field(const char* p, char* out, int outsz) {
	while (*p && *p <= ' ')
		++p;
	int i = 0;
	while (*p > ' ' && i < outsz - 1)
		out[i++] = *p++;
	out[i] = 0;
	return *p ? p + 1 : p;
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	const int fd = open("/proc", 0);
	if (fd < 0) {
		printf("ps: /proc: no such file or directory\n");
		return 1;
	}
	printf("%-6s %-16s %-6s %-6s %-6s %s\n", "PID", "COMMAND", "STATE", "PPID", "UTIME", "SIGNAL");
	for (uint32_t i = 0;; ++i) {
		vfs_dirent d;
		if (readdir(fd, i, &d) < 0)
			break;
		if (is_dot(d.name))
			continue;
		// /proc/<pid>/stat
		char path[64];
		char* p = path;
		for (const char* src = "/proc/"; *src; ++src)
			*p++ = *src;
		for (int k = 0; d.name[k] && k < 16; ++k)
			*p++ = d.name[k];
		for (const char* tail = "/stat"; *tail; ++tail)
			*p++ = *tail;
		*p = 0;

		const int sfd = open(path, 0);
		if (sfd < 0)
			continue;
		char line[256];
		const int n = read(sfd, line, sizeof line - 1);
		close(sfd);
		if (n <= 0)
			continue;
		line[n] = 0;

		const char* open_at = line;
		while (*open_at && *open_at != '(')
			++open_at;
		if (*open_at != '(')
			continue;
		char pid[16];
		int k = 0;
		for (const char* q = line; q < open_at && k < 15; ++q)
			pid[k++] = *q;
		pid[k] = 0;

		const char* body = open_at + 1;
		const char* close_at = body;
		while (*close_at && *close_at != ')')
			++close_at;
		char cmd[24];
		k = 0;
		for (const char* q = body; q < close_at && k < 23; ++q)
			cmd[k++] = *q;
		cmd[k] = 0;

		char state[8], ppid[8], utime[8], ktime[8], sig[16];
		const char* rest = *close_at ? close_at + 1 : close_at;
		rest = field(rest, state, sizeof state);
		rest = field(rest, ppid, sizeof ppid);
		rest = field(rest, utime, sizeof utime);
		rest = field(rest, ktime, sizeof ktime);
		rest = field(rest, sig, sizeof sig);

		unsigned u = 0, kk = 0;
		for (int q = 0; utime[q] >= '0' && utime[q] <= '9'; ++q)
			u = u * 10u + (unsigned)(utime[q] - '0');
		for (int q = 0; ktime[q] >= '0' && ktime[q] <= '9'; ++q)
			kk = kk * 10u + (unsigned)(ktime[q] - '0');
		char total[16];
		unsigned t = 0, w = 0;
		do {
			total[w++] = (char)('0' + (u + kk) % 10u);
			t = (u + kk) / 10u;
		} while (t);
		total[w] = 0;

		for (int a = 0, b = (int)w - 1; a < b; ++a, --b) {
			const char c = total[a];
			total[a] = total[b];
			total[b] = c;
		}

		printf("%-6s %-16s %-6s %-6s %-6s %s\n", pid, cmd, state, ppid, total, sig);
	}
	close(fd);
	return 0;
}
