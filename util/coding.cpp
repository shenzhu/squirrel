#include "coding.h"

namespace leveldb {

void EncodeFixed32(char* buf, uint32_t value) {
	if (port::kLittleEndian) {
		memcpy(buf, &value, sizeof(value));
	}
	else {
		buf[0] = value & 0xff;
		buf[1] = (value >> 8) & 0xff;
		buf[2] = (value >> 16) & 0xff;
		buf[3] = (value >> 24) & 0xff;
	}
}

void EncodeFixed64(char* buf, uint64_t value) {
	if (port::kLittleEndian) {
		memcpy(buf, &value, sizeof(value));
	}
	else {
		buf[0] = value & 0xff;
		buf[1] = (value >> 8) & 0xff;
		buf[2] = (value >> 16) & 0xff;
		buf[3] = (value >> 24) & 0xff;
		buf[4] = (value >> 32) & 0xff;
		buf[5] = (value >> 40) & 0xff;
		buf[6] = (value >> 48) & 0xff;
		buf[7] = (value >> 56) & 0xff;
	}
}

}  // namespace leveldb