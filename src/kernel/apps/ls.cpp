#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"
#include "vfs.h"

namespace apps {

static bool is_dot(const char* n) {
	return n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0));
}

int ls_main(int argc, const char** argv) {
	const char* path = argc > 1 ? argv[1] : fs::cwd();
	vfs::node* d = vfs::resolve(path);
	if (!d) {
		terminal::printf("ls: %s: no such file or directory\n", path);
		return 1;
	}
	if (d->type != vfs::kTypeDir) {
		terminal::printf("ls: %s: not a directory\n", path);
		return 1;
	}
	for (uint32_t i = 0;; ++i) {
		vfs::node out;
		if (vfs::readdir(d, i, &out) < 0)
			break;
		if (is_dot(out.name))
			continue;
		terminal::printf("%c %-14s %u bytes\n", out.type == vfs::kTypeDir ? '/' : ' ', out.name, out.size);
	}
	return 0;
}

} // namespace apps