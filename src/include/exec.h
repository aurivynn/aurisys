#pragma once

#include <stdint.h>

namespace exec {

bool replace(const char* path, int argc, const char* const* argv, int envc, const char* const* envp);

bool init_first(const char* path, int argc, const char* const* argv, int envc, const char* const* envp);

} // namespace exec
