#pragma once

namespace exec {

// load an elf from path and run it
bool run(const char* path, int argc, const char** argv);

} // namespace exec