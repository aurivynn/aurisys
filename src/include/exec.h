#pragma once

#include <stdint.h>

namespace exec {

struct fdmap {
	int child_fd;
	int from;
};

// read the elf at path, build the process around it and leave it runnable
// returns its pid or 0
uint32_t spawn(const char* path, int argc, const char** argv);

// as spawn, and the child gets `map` installed on its descriptors first
uint32_t spawn_mapped(const char* path, int argc, const char** argv, const fdmap* map, int nmap);

// spawn and wait for it. returns false if it never started. otherwise *exit_code gets the code it exited with, or 128 +
// the signal that killed it
bool run(const char* path, int argc, const char** argv, uint32_t* exit_code);

// as run, with descriptors installed in the child first
bool run_mapped(const char* path, int argc, const char** argv, const fdmap* map, int nmap, uint32_t* exit_code);

bool replace(const char* path, int argc, const char** argv);

} // namespace exec