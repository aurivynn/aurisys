#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int write_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: write <path> [words...]\n");
		return 1;
	}
	static char buf[2048];
	uint32_t n = 0;
	for (int i = 2; i < argc; ++i) {
		if (i > 2 && n < sizeof buf)
			buf[n++] = ' ';
		for (const char* s = argv[i]; *s && n < sizeof buf; ++s)
			buf[n++] = *s;
	}
	if (!fs::write_file(argv[1], buf, n)) {
		terminal::printf("write: failed (exists? out of space?)\n");
		return 1;
	}
	terminal::printf("wrote %u bytes to %s\n", n, argv[1]);
	return 0;
}

} // namespace apps