#pragma once

// interactive shell
namespace terminal {

void run(); // never returns
void printf(const char* fmt, ...);
void print(const char* string);

} // namespace terminal