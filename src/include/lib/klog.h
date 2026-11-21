#pragma once

#include <stdint.h>

namespace klog {

void putc(char c);
void printf(const char* fmt, ...);

uint32_t oldest();
int read(uint32_t off, void* buf, uint32_t len);

} // namespace klog
