#include "log_writer.h"

#include <stdint.h>
#include "env.h"
#include "coding.h"
#include "crc32c.h"

namespace leveldb {
namespace log {

static void InitTypeCrc(uint32_t* type_crc) {
	for (int i = 0; i <= kMaxRecordType; i++) {
		char t = static_cast<char>(i);
		type_crc[i] = crc32c::Value(&t, 1);
	}
}

Writer::Writer(WritableFile* dest)
	: dest_(dest),
	block_offset_(0) {
	InitTypeCrc(type_crc_);
}

Writer::Writer(WritableFile* dest, uint64_t dest_length)
	: dest_(dest), block_offset_(dest_length % kBlockSize) {
	InitTypeCrc(type_crc_);
}

Writer::~Writer() {
}

Status Writer::AddRecord(const Slice& slice) {
	const char* ptr = slice.data();
	size_t left = slice.size();

	// Fragment the record if necessary and emit it.  Note that if slice
	// is empty, we still want to iterate once to emit a single
	// zero-length record
	Status s;
	bool begin = true;
	do {
		const int leftover = kBlockSize - block_offset_;
		assert(leftover >= 0);
		if (leftover < kHeaderSize) {
			// Switch to a new block
			if (leftover > 0) {
				// File the trailer (literal below relies on kHeaderSize being 7)
				// 每个十六进制数字占1 byte，之前已经确认了当前block剩余的size小于7
				// 这个操作把当前block剩下的内容设置为0
				assert(kHeaderSize == 7);
				dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
			}
			block_offset_ = 0;
		}

		// Invariant: we never leave < kHeaderSize bytes in a block.
		assert(kBlockSize - block_offset_ - kHeaderSize >= 0);

		// 计算出当前block还可以放下的数据大小
		const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
		// 计算这个fragment的大小，即如果要写入数据当前record剩下的大小left小于avail，
		// 那么就把left数据完全写入，否则把当前block剩下的写满
		const size_t fragment_length = (left < avail) ? left : avail;

		RecordType type;
		// 如果剩下的数据size等于当前fragment的长度，则表示当前fragment可以在当前block写完
		const bool end = (left == fragment_length);
		if (begin && end) {
			type = kFullType;
		}
		else if (begin) {
			type = kFirstType;
		}
		else if (end) {
			type = kLastType;
		}
		else {
			type = kMiddleType;
		}

		s = EmitPhysicalRecord(type, ptr, fragment_length);
		ptr += fragment_length;
		left -= fragment_length;
		begin = false;
	} while (s.ok() && left > 0);
	return s;
}

Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr, size_t n) {
	assert(n <= 0xffff);  // Must fit in two bytes
	assert(block_offset_ + kHeaderSize + n <= kBlockSize);

	// Format the header
	char buf[kHeaderSize];
	buf[4] = static_cast<char>(n & 0xff);
	buf[5] = static_cast<char>(n >> 8);
	buf[6] = static_cast<char>(t);

	// Compute the crc of the record type and the payload
	uint32_t crc = crc32c::Extend(type_crc_[t], ptr, n);
	crc = crc32c::Mask(crc);                 // Adjust for storage
	EncodeFixed32(buf, crc);

	// Write the header and the payload
	Status s = dest_->Append(Slice(buf, kHeaderSize));
	if (s.ok()) {
		s = dest_->Append(Slice(ptr, n));
		if (s.ok()) {
			s = dest_->Flush();
		}
	}
	block_offset_ += kHeaderSize + n;
	return s;
}

}  // namespace log
}  // namespace leveldb