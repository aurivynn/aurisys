#pragma once

// app system
struct app {
	const char* name;
	const char* desc;
	int (*run)(int argc, const char** argv);
};

namespace apps {

const app* table();										// the registry
int count();											// how many apps
const app* find(const char* name);						// null if unknown
int run(const char* name, int argc, const char** argv); // -1 if unknown

} // namespace apps