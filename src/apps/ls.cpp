#include "lib.h"

#include "lib/datetime.h"

#include <stdint.h>

void mode_string(uint16_t mode, char out[11]) {
	const uint16_t fmt = mode & 0xF000;
	out[0] = fmt == 0x4000 ? 'd' : fmt == 0xA000 ? 'l' : fmt == 0x2000 ? 'c' : fmt == 0x6000 ? 'b' : '-';
	const char rwx[3] = {'r', 'w', 'x'};
	for (int i = 0; i < 9; ++i)
		out[1 + i] = (mode & (1u << (8 - i))) ? rwx[i % 3] : '-';
	out[10] = 0;
}

void fill_defaults(uint16_t* mode, uint16_t* links, uint8_t type) {
	if (!*mode)
		*mode = type == kTypeDir ? 0040755u : type == kTypeChar ? 0020666u : 0100644u;
	if (!*links)
		*links = 1;
}

void print_long(const char* name, uint16_t mode, uint16_t links, uint32_t size, uint32_t mtime) {
	char ms[11];
	mode_string(mode, ms);
	if (mtime) {
		const datetime::parts p = datetime::from_unix(mtime);
		printf("%s %3u %6u %04d-%02u-%02u %02u:%02u:%02u %s\n", ms, links, size, p.year, p.month, p.day, p.hour,
			   p.minute, p.second, name);
	} else {
		printf("%s %3u %6u %-16s %s\n", ms, links, size, "-", name);
	}
}

const char* base_name(const char* path) {
	const char* base = path;
	for (const char* p = path; *p; ++p)
		if (*p == '/')
			base = p + 1;
	return *base ? base : path;
}

int main(int argc, char** argv) {
	bool longf = false;
	const char* target = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-' && argv[i][1] == 'l' && argv[i][2] == 0)
			longf = true;
		else if (!target)
			target = argv[i];
	}

	char path[256];
	if (target) {
		size_t i = 0;
		while (target[i] && i < sizeof path - 1) {
			path[i] = target[i];
			++i;
		}
		path[i] = 0;
	} else if (getcwd(path, sizeof path) < 0) {
		strcpy(path, "/");
	}

	const int fd = open(path, 0);
	if (fd < 0) {
		printf("ls: %s: no such file or directory\n", path);
		return 1;
	}
	file_stat st;
	if (fstat(fd, &st) == 0 && st.type != kTypeDir) {
		if (longf) {
			fill_defaults(&st.mode, &st.links, st.type);
			print_long(base_name(path), st.mode, st.links, st.size, st.mtime);
			close(fd);
			return 0;
		}
		printf("ls: %s: not a directory\n", path);
		close(fd);
		return 1;
	}
	for (uint32_t i = 0;; ++i) {
		vfs_dirent d;
		if (readdir(fd, i, &d) < 0)
			break;
		if (is_dot(d.name))
			continue;
		if (longf) {
			fill_defaults(&d.mode, &d.links, d.type);
			print_long(d.name, d.mode, d.links, d.size, d.mtime);
		} else {
			printf("%c %-14s %u bytes\n", d.type == kTypeDir ? '/' : ' ', d.name, d.size);
		}
	}
	close(fd);
	return 0;
}
