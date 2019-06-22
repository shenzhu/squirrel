#ifndef STORAGE_LEVELDB_UTIL_CODING_H_
#define STORAGE_LEVELDB_UTIL_CODING_H_

#include <stdint.h>
#include <string.h>

#include <string>

#include "slice.h"
#include "port.h"

namespace leveldb {

// Lower-level versions of Put... that write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
extern void EncodeFixed32(char* dst, uint32_t value);
extern void EncodeFixed64(char* dst, uint64_t value);

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint32_t DecodeFixed32(const char* ptr) {
	if (port::kLittleEndian) {
		uint32_t result;
		memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
		return result;
	} else {
		return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
			    (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
				(static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
				(static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
	}
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_CODING_H_