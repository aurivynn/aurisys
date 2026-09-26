#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int cat_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: cat <path>\n");
		return 1;
	}
	uint32_t ino;
	if (!fs::lookup(argv[1], &ino)) {
		terminal::printf("cat: %s: no such file\n", argv[1]);
		return 1;
	}
	fs::stat st;
	if (!fs::getstat(ino, &st)) {
		terminal::printf("cat: stat failed\n");
		return 1;
	}
	if ((st.mode & 0xF000) != 0x8000) {
		terminal::printf("cat: %s: not a regular file\n", argv[1]);
		return 1;
	}
	char buf[256];
	uint32_t off = 0;
	for (;;) {
		const uint32_t n = fs::read(ino, buf, sizeof buf - 1, off);
		if (n == 0)
			break;
		buf[n] = 0;
		terminal::printf("%s", buf);
		off += n;
	}

	terminal::print("\n");
	return 0;
}

} // namespace apps