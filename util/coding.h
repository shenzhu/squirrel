#ifndef STORAGE_LEVELDB_UTIL_CODING_H_
#define STORAGE_LEVELDB_UTIL_CODING_H_

#include <stdint.h>
#include <string.h>

#include <string>

#include "slice.h"

namespace leveldb {

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint32_t DecodeFixed32(const char* ptr) {
  // Windows is little endian, remove the part for big endian
  // Load the raw bytes
  uint32_t result;
  memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
  return result;
  /*
  } else {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
  */
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_CODING_H_