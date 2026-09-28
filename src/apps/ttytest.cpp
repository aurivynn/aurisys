#include "lib.h"

static int ok(const char* what, int cond) {
	printf("ttytest: %s %s\n", what, cond ? "ok" : "FAILED");
	return cond ? 0 : 1;
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	int bad = 0;

	const int fd = open("/dev/tty", O_RDWR);
	printf("ttytest: /dev/tty is fd %d\n", fd);
	bad += ok("open /dev/tty", fd >= 0);
	if (fd < 0)
		return 1;

	uint32_t modes = 0;
	bad += ok("read the modes back", tty_get(fd, &modes) == 0);
	printf("ttytest: modes start as %s%s%s\n", (modes & kTtyCanon) ? "canon " : "", (modes & kTtyEcho) ? "echo " : "",
		   (modes & kTtySig) ? "sig" : "");

	bad += ok("go raw", tty_raw(fd) == 0);
	bad += ok("raw is every mode off", tty_get(fd, &modes) == 0 && modes == 0);

	printf("ttytest: type one character, no newline needed\n");
	char c = 0;
	const int n = read(fd, &c, 1);
	printf("ttytest: first raw read returned %d\n", n);
	bad += ok("raw mode gives a keystroke straight away", n == 1);
	if (n == 1)
		printf("ttytest: got 0x%02x\n", (uint8_t)c);

	printf("ttytest: press an arrow key\n");
	char seq[8];
	int got = 0;
	while (got < 3) {
		const int r = read(fd, &seq[got], 1);
		if (r != 1)
			break;
		if (seq[got] == 0x1b) {
			got = 1;
			continue;
		}
		++got;
	}
	bad += ok("an arrow key arrives as an escape sequence", got == 3 && seq[0] == 0x1b && seq[1] == '[');
	if (got == 3)
		printf("ttytest: sequence is %02x %c %c\n", (uint8_t)seq[0], seq[1], seq[2]);

	bad += ok("go canonical", tty_set(fd, kTtyCanon | kTtyEcho | kTtySig) == 0);
	printf("ttytest: type a line and press enter\n");
	char line[64];
	int total = 0;
	for (;;) {
		const int r = read(fd, &line[total], 1);
		if (r <= 0)
			break;
		total += r;
		if (line[total - 1] == '\n' || total >= (int)sizeof line - 1)
			break;
	}
	line[total] = 0;
	bad += ok("canonical mode delivers a whole line", total > 0 && line[total - 1] == '\n');
	printf("ttytest: read back %d bytes: %s", total, line);

	printf("ttytest: press ctrl-d at the start of a line\n");
	const int eof = read(fd, line, 1);
	bad += ok("ctrl-d on an empty line is end of file", eof == 0);

	close(fd);
	printf("ttytest: %d failure(s)\n", bad);
	return bad ? 1 : 0;
}
