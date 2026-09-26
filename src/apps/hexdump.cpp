#include "lib.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("usage: hexdump <path>\n");
		return 1;
	}
	const int fd = open(argv[1], 0);
	if (fd < 0) {
		printf("hexdump: %s: no such file\n", argv[1]);
		return 1;
	}
	file_stat st;
	if (fstat(fd, &st) < 0 || st.type != kTypeFile) {
		printf("hexdump: %s: not a regular file\n", argv[1]);
		close(fd);
		return 1;
	}
	uint8_t buf[16];
	uint32_t off = 0;
	for (;;) {
		const int n = read(fd, buf, sizeof buf);
		if (n <= 0)
			break;
		printf("%08x  ", off);
		for (uint32_t i = 0; i < 16; ++i)
			printf(i < (uint32_t)n ? "%02x " : "   ", i < (uint32_t)n ? (int)buf[i] : 0);
		printf("  ");
		for (uint32_t i = 0; i < (uint32_t)n; ++i)
			printf("%c", (buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '.');
		printf("\n");
		off += (uint32_t)n;
	}
	close(fd);
	return 0;
}