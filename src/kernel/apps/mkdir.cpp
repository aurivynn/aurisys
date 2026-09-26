#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int mkdir_main(int argc, const char** argv) {
	if (argc != 2) {
		terminal::printf("usage: mkdir <path>\n");
		return 1;
	}
	if (!fs::mkdir(argv[1])) {
		terminal::printf("mkdir: %s: failed (exists? bad parent?)\n", argv[1]);
		return 1;
	}
	return 0;
}

} // namespace apps