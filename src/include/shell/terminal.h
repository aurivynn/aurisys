#pragma once

// interactive shell
namespace terminal {

void run();						   // never returns
void printf(const char* fmt, ...); // app output, screen + serial

} // namespace terminal