#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	const int fd = open("/dev/kmsg", 0);
	if (fd < 0) {
		printf("dmesg: /dev/kmsg: no such file\n");
		return 1;
	}
	char buf[256];
	for (;;) {
		const int n = read(fd, buf, sizeof buf);
		if (n <= 0)
			break;
		write(1, buf, (uint32_t)n);
	}
	close(fd);
	return 0;
}
