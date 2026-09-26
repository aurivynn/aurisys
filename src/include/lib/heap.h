#pragma once

#include "arch/bootinfo.h"

#include <stddef.h>
#include <stdint.h>

// a real allocator

void heap_init(const bootinfo* bi);
void* kmalloc(size_t size); // 16b aligned, nullptr if out of memory
void* kcalloc(size_t n, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);				  // null is a no op, anything else must be ours
void* kframe_alloc();				  // one 4k page aligned frame, nullptr if out
void* kframe_alloc_n(uint32_t pages); // n contiguous page aligned frames
void kframe_free(void* frame);		  // give a frame back, must come from kframe_alloc

// largest block the allocator would hand out, so a caller can tell a nearly full heap from a fragmented one
size_t heap_largest_free();

void heap_stats(size_t* total, size_t* used, size_t* largest_free);

// how many calls landed in the slow paths
void heap_counters(size_t* allocs, size_t* frees, size_t* coalesced, size_t* frames);
