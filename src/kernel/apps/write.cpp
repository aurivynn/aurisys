#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int write_main(int argc, const char** argv) {
	uint32_t flags = 0;
	int first = 1;
	if (argc >= 3 && argv[1][0] == '-') {
		if (argv[1][1] == 'a')
			flags |= fs::kWriteAppend;
		else if (argv[1][1] == 't')
			flags |= fs::kWriteTrunc;
		else {
			terminal::printf("usage: write [-a|-t] <path> [words...]\n");
			return 1;
		}
		first = 2;
	}
	if (argc < first + 1) {
		terminal::printf("usage: write [-a|-t] <path> [words...]\n");
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
	if (!fs::write_file(argv[first], buf, n, flags)) {
		terminal::printf("write: failed (exists? out of space?)\n");
		return 1;
	}
	terminal::printf("wrote %u bytes to %s\n", n, argv[first]);
	return 0;
}

} // namespace apps