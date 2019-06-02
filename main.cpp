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

int main() {
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

	// Crc32c
	uint32_t crc = { 100 };
	const char* crc32cChar = new char('A');
	std::cout << "Test of crc32: "
		<< leveldb::crc32c::Extend(crc, crc32cChar, 1)
		<< std::endl;

	// Hash
	std::cout << leveldb::Hash("hash", 4, 7) << std::endl;

	// Bloom filter
	auto bloomFilter = leveldb::NewBloomFilterPolicy(10);
	std::cout << bloomFilter->Name() << std::endl;

	// Logging
	std::cout << leveldb::NumberToString(100) << std::endl;

	std::cout << "Hello World" << std::endl;

    return 0;
}
