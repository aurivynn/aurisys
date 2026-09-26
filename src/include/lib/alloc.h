#pragma once

// the app side allocator. a free list sitting on top of sbrk, so a process
// that allocates a lot still cannot reach past its own arena
//
// blocks carry a header, the same idea as the kernel heap but user side
// and much smaller. free() puts a block back and merges it with a free
// neighbour so a long lived process does not fragment itself into nothing

#include <stddef.h>
#include <stdint.h>

extern "C" {

void* malloc(size_t n);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t n);
void free(void* p);

} // extern "C"

namespace alloc {

// header is a size and a next, 8 bytes on 32 bit, so the payload stays
// 8b aligned without a second pad field
struct Block {
	uint32_t size; // total bytes including this header. 0 means free-listed
	Block* next;	 // free blocks only, threaded through here
};

constexpr uint32_t kAlign = 8;
constexpr uint32_t kMinBlock = 16;

inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + (a - 1u)) & ~(a - 1u); }
inline uint32_t align_up(uint32_t v) { return align_up(v, kAlign); }
inline uint8_t* payload_of(Block* b) { return (uint8_t*)b + sizeof(Block); }
inline Block* block_of(void* p) { return (Block*)((uint8_t*)p - sizeof(Block)); }

} // namespace alloc
