#include "lib.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("usage: cat <path>\n");
		return 1;
	}
	const int fd = open(argv[1], 0);
	if (fd < 0) {
		printf("cat: %s: no such file\n", argv[1]);
		return 1;
	}
	file_stat st;
	if (fstat(fd, &st) == 0 && st.type == kTypeDir) {
		printf("cat: %s: not a regular file\n", argv[1]);
		close(fd);
		return 1;
	}
	char buf[256];
	for (;;) {
		const int r = read(fd, buf, sizeof buf - 1);
		if (r <= 0)
			break;
		buf[r] = 0;
		printf("%s", buf);
	}
	close(fd);
	printf("\n");
	return 0;
}