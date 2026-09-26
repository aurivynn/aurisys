#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	const int fd = open("/bin", 0);
	if (fd < 0) {
		printf("no /bin?\n");
		return 1;
	}
	for (uint32_t i = 0;; ++i) {
		vfs_dirent d;
		if (readdir(fd, i, &d) < 0)
			break;
		if (is_dot(d.name))
			continue;
		printf("  %s\n", d.name);
	}
	close(fd);
	return 0;
}