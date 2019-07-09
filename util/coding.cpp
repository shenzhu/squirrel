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
		// 把低8位(1010 1100)给内存低字节
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

char* EncodeVarint64(char* dst, uint64_t v) {
	static const int B = 128;
	unsigned char* ptr = reinterpret_cast<unsigned char*>(dst);
	while (v >= B) {
		// A & (B - 1) = A % B
		*(ptr++) = (v & (B - 1)) | B;
		v >>= 7;
	}
	*(ptr++) = static_cast<unsigned char>(v);
	return reinterpret_cast<char*>(ptr);
}

void PutVarint64(std::string* dst, uint64_t v) {
	// 对于64位整型，最多需要10个字节(10*8 - 10(标志位) > 64)
	char buf[10];
	char* ptr = EncodeVarint64(buf, v);
	dst->append(buf, ptr - buf);
}

int VarintLength(uint64_t v) {
	int len = 1;
	while (v >= 128) {
		v >>= 7;
		len++;
	}
	return len;
}

const char* GetVarint32PtrFallback(const char* p,
	const char* limit,
	uint32_t* value) {
	uint32_t result = 0;
	for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
		uint32_t byte = *(reinterpret_cast<const unsigned char*>(p));
		p++;
		if (byte & 128) {
			// More bytes are present
			result |= ((byte & 127) << shift);
		}
		else {
			result |= (byte << shift);
			*value = result;
			return reinterpret_cast<const char*>(p);
		}
	}
	return NULL;
}

bool GetVarint32(Slice* input, uint32_t* value) {
	const char* p = input->data();
	const char* limit = p + input->size();
	const char* q = GetVarint32Ptr(p, limit, value);
	if (q == NULL) {
		return false;
	}
	else {
		*input = Slice(q, limit - q);
		return true;
	}
}

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
	uint64_t result = 0;
	for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
		uint64_t byte = *(reinterpret_cast<const unsigned char*>(p));
		p++;
		if (byte & 128) {
			// More bytes are present
			result |= ((byte & 127) << shift);
		}
		else {
			result |= (byte << shift);
			*value = result;
			return reinterpret_cast<const char*>(p);
		}
	}
	return NULL;
}

bool GetVarint64(Slice* input, uint64_t* value) {
	const char* p = input->data();
	const char* limit = p + input->size();
	const char* q = GetVarint64Ptr(p, limit, value);
	if (q == NULL) {
		return false;
	}
	else {
		*input = Slice(q, limit - q);
		return true;
	}
}


}  // namespace leveldb