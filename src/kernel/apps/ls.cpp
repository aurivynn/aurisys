#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

static bool list_cb(const char* name, uint32_t ino, uint8_t type, void* ctx) {
	(void)ctx;
	fs::stat st;
	fs::getstat(ino, &st);
	terminal::printf("%c %-14s %u bytes\n", type == 2 ? '/' : ' ', name, st.size);
	return true;
}

int ls_main(int argc, const char** argv) {
	const char* path = argc > 1 ? argv[1] : fs::cwd();
	uint32_t ino;
	if (!fs::lookup(path, &ino)) {
		terminal::printf("ls: %s: no such file or directory\n", path);
		return 1;
	}
	const int n = fs::list_dir(ino, list_cb, nullptr);
	if (n < 0) {
		terminal::printf("ls: %s: not a directory\n", path);
		return 1;
	}
	return 0;
}

} // namespace apps