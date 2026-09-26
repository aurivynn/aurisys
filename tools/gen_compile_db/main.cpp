#include <dirent.h>
#include <sys/stat.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static void collect(const std::string& dir, std::vector<std::string>& out) {
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		const std::string p = dir + "/" + e->d_name;
		struct stat st;
		if (stat(p.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			collect(p, out);
		} else if (S_ISREG(st.st_mode)) {
			const size_t n = strlen(e->d_name);
			if (n > 4 && strcmp(e->d_name + n - 4, ".cpp") == 0)
				out.push_back(p);
		}
	}
	closedir(d);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: gen_compile_db <cxx> <flags>\n");
		return 1;
	}
	const std::string cxx = argv[1];
	const std::string flags = argv[2];

	std::vector<std::string> files;
	collect("src/kernel", files);
	std::sort(files.begin(), files.end());

	char buf[4096];
	if (!getcwd(buf, sizeof(buf)))
		buf[0] = 0;
	const std::string root = buf;

	FILE* f = fopen("compile_commands.json", "w");
	if (!f) {
		perror("fopen");
		return 1;
	}
	fprintf(f, "[\n");
	for (size_t i = 0; i < files.size(); ++i) {
		const std::string& src = files[i];
		std::string obj = "build/" + src.substr(strlen("src/kernel/"));
		const size_t dot = obj.rfind(".cpp");
		obj = obj.substr(0, dot) + ".o";
		fprintf(f, "  {\n    \"directory\": \"%s\",\n", root.c_str());
		fprintf(f, "    \"file\": \"%s\",\n", (root + "/" + src).c_str());
		fprintf(f, "    \"command\": \"%s %s -c %s -o %s\"\n", cxx.c_str(), flags.c_str(), src.c_str(), obj.c_str());
		fprintf(f, "  }%s\n", i + 1 < files.size() ? "," : "");
	}
	fprintf(f, "]\n");
	fclose(f);
	return 0;
}