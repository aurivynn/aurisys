#pragma once

#include "arch/bootinfo.h"

#include <stddef.h>
#include <stdint.h>

// bump allocator over the usable e820 region past the kernel image.
void heap_init(const bootinfo* bi);
void* kmalloc(size_t size); // 8b aligned, nullptr if out of memory
void* kframe_alloc();		// one 4k page aligned frame, nullptr if out
void kfree(void* ptr);		// bump allocator
void heap_stats(size_t* total, size_t* used, size_t* largest_free);