#include "lib/heap.h"

#include <stddef.h>

void* operator new(size_t n) { return kmalloc(n); }

void* operator new[](size_t n) { return kmalloc(n); }

void operator delete(void* p) noexcept { kfree(p); }

void operator delete[](void* p) noexcept { kfree(p); }

void operator delete(void* p, size_t) noexcept { kfree(p); }

void operator delete[](void* p, size_t) noexcept { kfree(p); }