#include "lib.h"

static int slurp(int in, int out) {
	char buf[256];
	for (;;) {
		const int r = read(in, buf, sizeof buf - 1);
		if (r <= 0)
			break;
		buf[r] = 0;
		printf("%s", buf);
	}
	return 0;
}

int main(int argc, char** argv) {
	if (argc < 2)
		return slurp(0, 1);
	for (int i = 1; i < argc; ++i) {
		const int fd = open(argv[i], 0);
		if (fd < 0) {
			printf("cat: %s: no such file\n", argv[i]);
			return 1;
		}
		file_stat st;
		if (fstat(fd, &st) == 0 && st.type == kTypeDir) {
			printf("cat: %s: not a regular file\n", argv[i]);
			close(fd);
			return 1;
		}
		slurp(fd, 1);
		close(fd);
	}
	return 0;
}
