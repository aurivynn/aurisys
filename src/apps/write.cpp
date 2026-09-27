#include "lib.h"

#include "syscall.h"

int main(int argc, char** argv) {
	uint32_t flags = 0;
	int first = 1;
	if (argc >= 3 && argv[1][0] == '-') {
		if (argv[1][1] == 'a')
			flags |= O_APPEND;
		else if (argv[1][1] == 't')
			flags |= O_TRUNC;
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

	flags |= O_WRONLY | O_CREAT;
	if (!(flags & O_APPEND))
		flags |= O_TRUNC;
	const int fd = open(argv[first], flags);
	if (fd < 0) {
		printf("write: %s: cannot open\n", argv[first]);
		return 1;
	}
	file_stat st;
	if (fstat(fd, &st) == 0 && st.type == kTypeChar) {
		write(fd, buf, n);
		close(fd);
		printf("wrote %u bytes to %s\n", n, argv[first]);
		return 0;
	}
	const int wrote = write(fd, buf, n);
	close(fd);
	if (wrote < 0) {
		printf("write: %s: failed (out of space?)\n", argv[first]);
		return 1;
	}
	printf("wrote %u bytes to %s\n", n, argv[first]);
	return 0;
}