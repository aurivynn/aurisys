#include "apps/app.h"

#include "shell/terminal.h"
#include "vfs.h"

namespace apps {

int cat_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: cat <path>\n");
		return 1;
	}
	vfs::node* n = vfs::resolve(argv[1]);
	if (!n) {
		terminal::printf("cat: %s: no such file\n", argv[1]);
		return 1;
	}
	if (n->type == vfs::kTypeDir) {
		terminal::printf("cat: %s: not a regular file\n", argv[1]);
		return 1;
	}
	if (!n->read) {
		terminal::printf("cat: %s: not readable\n", argv[1]);
		return 1;
	}
	char buf[256];
	uint32_t off = 0;
	for (;;) {
		const int r = n->read(n, buf, off, sizeof buf - 1);
		if (r <= 0)
			break;
		buf[r] = 0;
		terminal::printf("%s", buf);
		off += (uint32_t)r;
	}
	terminal::print("\n");
	return 0;
}

} // namespace apps