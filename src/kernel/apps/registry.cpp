// the app registry
#include "apps/app.h"

namespace apps {

int help_main(int argc, const char** argv);
int echo_main(int argc, const char** argv);
int mem_main(int argc, const char** argv);
int uptime_main(int argc, const char** argv);
int panic_main(int argc, const char** argv);

const app kTable[] = {
	{"help", "list the apps", help_main},	   {"echo", "print the args", echo_main},
	{"mem", "heap usage", mem_main},		   {"uptime", "ms since boot", uptime_main},
	{"panic", "fault on purpose", panic_main},
};

const app* table() { return kTable; }

int count() { return sizeof(kTable) / sizeof(kTable[0]); }

static bool str_eq(const char* a, const char* b) {
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

const app* find(const char* name) {
	for (int i = 0; i < count(); ++i)
		if (str_eq(kTable[i].name, name))
			return &kTable[i];
	return nullptr;
}

int run(const char* name, int argc, const char** argv) {
	const app* a = find(name);
	if (!a)
		return -1;
	return a->run(argc, argv);
}

} // namespace apps