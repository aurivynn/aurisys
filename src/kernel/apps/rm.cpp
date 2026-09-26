#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"
#include "vfs.h"

namespace apps {

int rm_main(int argc, const char** argv) {
	if (argc < 2) {
		terminal::printf("usage: rm <path> [path...]\n");
		return 1;
	}
	for (int i = 1; i < argc; ++i) {
		vfs::node* n = vfs::resolve(argv[i]);
		if (n && n->mount) {
			terminal::printf("rm: %s: is a mount point\n", argv[i]);
			return 1;
		}
		if (!fs::rm(argv[i])) {
			terminal::printf("rm: %s: failed (missing? not empty?)\n", argv[i]);
			return 1;
		}
	}
	return 0;
}

} // namespace apps