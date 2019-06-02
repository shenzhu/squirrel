#include "arena.h"

namespace leveldb {

static const int kBlockSize{ 4096 };

Arena::Arena() : alloc_ptr_(nullptr), alloc_bytes_remaining_(0), blocks_memory_(0) {
}

Arena::~Arena() {
	for (auto i = 0; i < blocks_.size(); ++i) {
		delete[] blocks_[i];
	}
}

char* Arena::AllocateFallback(size_t bytes) {
	if (bytes > kBlockSize / 4) {
		auto result = AllocateNewBlock(bytes);
		return result;
	}

	// bytes < 1 / 4 (1024 KB), allocate a new block(4096)
	alloc_ptr_ = AllocateNewBlock(kBlockSize);
	alloc_bytes_remaining_ = kBlockSize;

	auto result = alloc_ptr_;
	alloc_ptr_ += bytes;
	alloc_bytes_remaining_ -= bytes;
	return result;
}

char* Arena::AllocateNewBlock(size_t blocks_bytes) {
	auto result = new char[blocks_bytes];
	blocks_memory_ += blocks_bytes;
	blocks_.push_back(result);
	return result;
}

char* Arena::AllocateAligned(size_t bytes) {
	const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
	assert((align & (align - 1)) == 0);

	// A & (B - 1) = A % B
	auto current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);
	auto slop = (current_mod == 0 ? 0 : (align - current_mod));
	auto needed = bytes + slop;

	decltype(alloc_ptr_) result;
	if (needed <= alloc_bytes_remaining_) {
		result = alloc_ptr_ + slop;
		alloc_ptr_ += needed;
		alloc_bytes_remaining_ -= needed;
	}
	else {
		result = AllocateFallback(bytes);
	}
	assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);

	return result;
}

} // close namespace leveldb
