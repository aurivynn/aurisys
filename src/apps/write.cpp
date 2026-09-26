#include "lib.h"

#include "syscall.h"

int main(int argc, char** argv) {
	uint32_t flags = 0;
	int first = 1;
	if (argc >= 3 && argv[1][0] == '-') {
		if (argv[1][1] == 'a')
			flags |= kWriteAppend;
		else if (argv[1][1] == 't')
			flags |= kWriteTrunc;
		else {
			printf("usage: write [-a|-t] <path> [words...]\n");
			return 1;
		}
		first = 2;
	}
	if (argc < first + 1) {
		printf("usage: write [-a|-t] <path> [words...]\n");
		return 1;
	}
	static char buf[2048];
	uint32_t n = 0;
	for (int i = first + 1; i < argc; ++i) {
		if (i > first + 1 && n < sizeof buf)
			buf[n++] = ' ';
		for (const char* s = argv[i]; *s && n < sizeof buf; ++s)
			buf[n++] = *s;
	}
	const int fd = open(argv[first], 0);
	if (fd >= 0) {
		file_stat st;
		if (fstat(fd, &st) == 0 && st.type == kTypeChar) {
			write(fd, buf, n);
			printf("wrote %u bytes to %s\n", n, argv[first]);
			close(fd);
			return 0;
		}
		close(fd);
	}
	if (writefile(argv[first], buf, n, flags) < 0) {
		printf("write: failed (exists? out of space?)\n");
		return 1;
	}
	printf("wrote %u bytes to %s\n", n, argv[first]);
	return 0;
}