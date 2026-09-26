#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int hexdump_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: hexdump <path>\n");
		return 1;
	}
	uint32_t ino;
	if (!fs::lookup(argv[1], &ino)) {
		terminal::printf("hexdump: %s: no such file\n", argv[1]);
		return 1;
	}
	fs::stat st;
	if (!fs::getstat(ino, &st) || (st.mode & 0xF000) != 0x8000) {
		terminal::printf("hexdump: %s: not a regular file\n", argv[1]);
		return 1;
	}
	uint8_t buf[16];
	uint32_t off = 0;
	for (;;) {
		const uint32_t n = fs::read(ino, buf, sizeof buf, off);
		if (n == 0)
			break;
		terminal::printf("%08x  ", off);
		for (uint32_t i = 0; i < 16; ++i)
			terminal::printf(i < n ? "%02x " : "   ", i < n ? (int)buf[i] : 0);
		terminal::printf("  ");
		for (uint32_t i = 0; i < n; ++i)
			terminal::printf("%c", (buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '.');
		terminal::printf("\n");
		off += n;
	}
	return 0;
}

} // namespace apps