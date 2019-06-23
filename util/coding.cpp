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

void PutFixed32(std::string* dst, uint32_t value) {
	char buf[sizeof(value)];
	EncodeFixed32(buf, value);
	dst->append(buf, sizeof(buf));
}

void PutFixed64(std::string* dst, uint64_t value) {
	char buf[sizeof(value)];
	EncodeFixed64(buf, value);
	dst->append(buf, sizeof(buf));
}

char* EncodeVarint32(char* dst, uint32_t v) {
	// Operate on characters as unsigneds
	unsigned char* ptr = reinterpret_cast<unsigned char*>(dst);
	static const int B = 128;
	if (v < (1 << 7)) {
		// 如果v小于128，直接encode就可以
		// 因为在varint编码的情况下，1个byte最高能表示的数据大小为127
		// 这时直接写入即可
		*(ptr++) = v;
	}
	else if (v < (1 << 14)) {
		// 如果v小于16384，假如v=300(0000 0001 0010 1100)
		// 先和128(0000 0000 1000 0000)按位或,
		// 得到0000 0001 1010 1100
		// 把低8位（1010 1100）给内存低字节
		*(ptr++) = v | B;
		// 把300(0000 0001 0010 1100)右移7位得到000 0000 0000 0001 0给内存高字节
		*(ptr++) = v >> 7;
	}
	else if (v < (1 << 21)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = v >> 14;
	}
	else if (v < (1 << 28)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = v >> 21;
	}
	else {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = (v >> 21) | B;
		*(ptr++) = v >> 28;
	}
	return reinterpret_cast<char*>(ptr);
}

void PutVarint32(std::string* dst, uint32_t v) {
	// 在varint编码的情况下，一个uint32_t数字占用的最大空间为5
	char buf[5];
	char* ptr = EncodeVarint32(buf, v);
	dst->append(buf, ptr - buf);
}

}  // namespace leveldb