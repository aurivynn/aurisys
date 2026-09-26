#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int cd_main(int argc, const char** argv) {
	if (argc > 2) {
		terminal::printf("usage: cd [dir]\n");
		return 1;
	}
	const char* where = argc == 2 ? argv[1] : "/";
	if (!fs::chdir(where)) {
		terminal::printf("cd: %s: not a directory\n", where);
		return 1;
	}
	return 0;
}

} // namespace apps