#include "lib.h"

int main(int argc, char** argv) {
	char path[256];
	if (argc > 1) {
		size_t i = 0;
		while (argv[1][i] && i < sizeof path - 1) {
			path[i] = argv[1][i];
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
		printf("%c %-14s %u bytes\n", d.type == kTypeDir ? '/' : ' ', d.name, d.size);
	}
	close(fd);
	return 0;
}