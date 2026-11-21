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
		*mode = type == kTypeDir ? 0040755u : type == kTypeChar ? 0020666u : type == kTypeLink ? 0120777u : 0100644u;
	if (!*links)
		*links = 1;
}

void join_path(char* out, uint32_t outsz, const char* dir, const char* name) {
	uint32_t o = 0;
	for (const char* p = dir; *p && o + 1 < outsz; ++p)
		out[o++] = *p;
	if (o == 0 || out[o - 1] != '/') {
		if (o + 1 < outsz)
			out[o++] = '/';
	}
	for (const char* p = name; *p && o + 1 < outsz; ++p)
		out[o++] = *p;
	out[o] = 0;
}

// name -> target, for the long listing
void link_name(char* out, uint32_t outsz, const char* name, const char* target) {
	uint32_t o = 0;
	for (const char* p = name; *p && o + 1 < outsz; ++p)
		out[o++] = *p;
	const char arrow[] = " -> ";
	for (uint32_t i = 0; i < sizeof arrow - 1 && o + 1 < outsz; ++i)
		out[o++] = arrow[i];
	for (const char* p = target; *p && o + 1 < outsz; ++p)
		out[o++] = *p;
	out[o] = 0;
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

bool dirent_for(const char* path, vfs_dirent* out) {
	const char* base = base_name(path);
	char dir[256];
	if (base == path) {
		strcpy(dir, ".");
	} else {
		const uint32_t n = (uint32_t)(base - path);
		if (n >= sizeof dir)
			return false;
		memcpy(dir, path, n);
		dir[n] = 0;
	}
	const int fd = open(dir, 0);
	if (fd < 0)
		return false;
	bool found = false;
	for (uint32_t i = 0;; ++i) {
		vfs_dirent d;
		if (readdir(fd, i, &d) < 0)
			break;
		if (strcmp(d.name, base) == 0) {
			*out = d;
			found = true;
			break;
		}
	}
	close(fd);
	return found;
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

	if (longf) {
		char target[256];
		if (readlink(path, target, sizeof target) >= 0) {
			char shown[384];
			link_name(shown, sizeof shown, base_name(path), target);
			vfs_dirent d;
			if (dirent_for(path, &d)) {
				fill_defaults(&d.mode, &d.links, d.type);
				print_long(shown, d.mode, d.links, d.size, d.mtime);
			} else {
				print_long(shown, 0120777, 1, (uint32_t)strlen(target), 0);
			}
			return 0;
		}
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
			char shown[384];
			const char* name = d.name;
			char target[256];
			if (d.type == kTypeLink) {
				char full[256];
				join_path(full, sizeof full, path, d.name);
				if (readlink(full, target, sizeof target) >= 0) {
					link_name(shown, sizeof shown, d.name, target);
					name = shown;
					d.size = (uint32_t)strlen(target);
				}
			}
			print_long(name, d.mode, d.links, d.size, d.mtime);
		} else {
			const char kind = d.type == kTypeDir ? '/' : d.type == kTypeLink ? '@' : ' ';
			printf("%c %-14s %u bytes\n", kind, d.name, d.size);
		}
	}
	close(fd);
	return 0;
}
