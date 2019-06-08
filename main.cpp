#include <iostream>
#include "arena.h"
#include "random.h"
#include "slice.h"
#include "status.h"
#include "crc32c.h"
#include "hash.h"
#include "filter_policy.h"
#include "mutexlock.h"
#include "logging.h"
#include "cache.h"
#include "testharness.h"

int main() {
	/*
	// Arena
	leveldb::Arena arena;

	// Random
	leveldb::Random random{ 7 };
	std::cout << random.Next() << std::endl;

	// Slice
	leveldb::Slice slice1{ "sliceOne" };
	leveldb::Slice slice2{ "sliceTwo" };
	std::cout << slice1.data() << " "
		<< slice2.data() << " "
		<< slice1.compare(slice2) << std::endl;

	// Status
	leveldb::Slice sliceNotFound{ "NotFound" };
	auto status = leveldb::Status::NotFound(sliceNotFound);
	std::cout << status.ToString() << std::endl;

	// Bloom filter
	auto bloomFilter = leveldb::NewBloomFilterPolicy(10);
	std::cout << bloomFilter->Name() << std::endl;

	// Logging
	std::cout << leveldb::NumberToString(100) << std::endl;

	// Cache
	auto lruCache = leveldb::NewLRUCache(10);
	*/

	leveldb::test::RunAllTests();
	std::cout << "Hello World" << std::endl;

    return 0;
}
