#include "apps/app.h"

#include "shell/terminal.h"
#include "vfs.h"

namespace apps {

int cat_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: cat <path>\n");
		return 1;
	}
	const int fd = vfs::fd_open(argv[1], 0);
	if (fd < 0) {
		terminal::printf("cat: %s: no such file\n", argv[1]);
		return 1;
	}
	vfs::node* n = vfs::fd_node(fd);
	if (n->type == vfs::kTypeDir) {
		terminal::printf("cat: %s: not a regular file\n", argv[1]);
		vfs::fd_close(fd);
		return 1;
	}
	if (!n->read) {
		terminal::printf("cat: %s: not readable\n", argv[1]);
		vfs::fd_close(fd);
		return 1;
	}
	char buf[256];
	for (;;) {
		const int r = vfs::fd_read(fd, buf, sizeof buf - 1);
		if (r <= 0)
			break;
		buf[r] = 0;
		terminal::printf("%s", buf);
	}
	vfs::fd_close(fd);
	terminal::print("\n");
	return 0;
}

} // namespace apps