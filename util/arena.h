#ifndef STORATE_LEVELDB_UTIL_ARENA_H_
#define STORATE_LEVELDB_UTIL_ARENA_H_

#include <cassert>
#include <cstddef>
#include <vector>

namespace leveldb {

class Arena {
public:
	Arena();
	~Arena();

	// Return a pointer to a newly allocated memory block of "bytes bytes
	char* Allocate(size_t bytes);

	// Allocate memory with the normal alignment guarantees provided by malloc
	char* AllocateAligned(size_t bytes);

	// Return an estimate of the total memory usage of data allocated by arena,
	// including allocated but not yest used memory
	size_t MemoryUsage() const {
		return blocks_memory_ + blocks_.capacity() * sizeof(char*);
	}

	// Avoid copy
	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;

private:
	// Allocate state
	char* alloc_ptr_;
	size_t alloc_bytes_remaining_;

	// Array of allocated memory blocks
	std::vector<char*> blocks_;

	// Memory allocated so far in bytes
	size_t blocks_memory_;

	char* AllocateFallback(size_t bytes);
	char* AllocateNewBlock(size_t block_bytes);
};

inline char* Arena::Allocate(size_t bytes) {
	assert(bytes > 0);
	if (bytes <= alloc_bytes_remaining_) {
		auto result = alloc_ptr_;
		alloc_ptr_ += bytes;
		alloc_bytes_remaining_ -= bytes;
		return result;
	}
	return AllocateFallback(bytes);
}

} // close namespace leveldb

#endif // !STORATE_LEVELDB_UTIL_ARENA_H_
