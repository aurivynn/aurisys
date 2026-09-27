#pragma once

#include <stdint.h>

namespace exec {

// read the elf at path, build the process around it and leave it runnable
// returns its pid or 0
uint32_t spawn(const char* path, int argc, const char** argv);

// spawn and wait for it. returns false if it never started. otherwise *exit_code gets the code it exited with, or 128 +
// the signal that killed it
bool run(const char* path, int argc, const char** argv, uint32_t* exit_code);

} // namespace exec