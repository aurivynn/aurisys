#pragma once

#include <stdarg.h>

namespace print {

typedef void (*emit_fn)(char);
void vprintf(emit_fn emit, const char* fmt, va_list ap);

} // namespace print