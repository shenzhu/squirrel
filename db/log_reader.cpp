#include "log_reader.h"

#include <stdio.h>
#include "env.h"
#include "coding.h"
#include "crc32c.h"

namespace leveldb {
namespace log {

Reader::Reporter::~Reporter() {
}

Reader::Reader(SequentialFile* file, Reporter* reporter, bool checksum,
	uint64_t initial_offset)
	: file_(file),
	reporter_(reporter),
	checksum_(checksum),
	backing_store_(new char[kBlockSize]),
	buffer_(),
	eof_(false),
	last_record_offset_(0),
	end_of_buffer_offset_(0),
	initial_offset_(initial_offset),
	resyncing_(initial_offset > 0) {
}

Reader::~Reader() {
	delete[] backing_store_;
}

bool Reader::SkipToInitialBlock() {
	// 因为initial_offset的大小可能大于block size，所以要计算
	// 一下在block中的偏移量
	size_t offset_in_block = initial_offset_ % kBlockSize;
	// 计算下一个block的地址
	// 这个减法是计算之前所有需要跳过的block中最后一个的结束地址，这样也就得到了
	// 下一个要开始的block的地址
	uint64_t block_start_location = initial_offset_ - offset_in_block;

	// Don't search a block if we'd be in the trailer
	// 如果block中的偏移量已经大于block size - 6(这已经是每个block能剩余的最小数量)
	// 那么就跳到下一个block
	if (offset_in_block > kBlockSize - 6) {
		offset_in_block = 0;
		block_start_location += kBlockSize;
	}

	end_of_buffer_offset_ = block_start_location;

	// Skip to start of first block that can contain initial record
	if (block_start_location > 0) {
		Status skip_status = file_->Skip(block_start_location);
		if (!skip_status.ok()) {
			ReportDrop(block_start_location, skip_status);
			return false;
		}
	}

	return true;
}

bool Reader::ReadRecord(Slice* record, std::string* scratch) {
	if (last_record_offset_ < initial_offset_) {
		if (!SkipToInitialBlock()) {
			return false;
		}
	}

	scratch->clear();
	record->clear();
	// 上条记录是否为完整记录
	bool in_fragmented_record = false;
	// Record offset of the logical record that we're reading
	// 0 is a dummy value to make compilers happy
	// 当前读取记录的偏移量
	uint64_t prospective_record_offset = 0;

	Slice fragment;
	while (true) {
		const unsigned int record_type = ReadPhysicalRecord(&fragment);

		// ReadPhysicalRecord may have only had an empty trailer remaining in its
		// internal buffer. Calculate the offset of the next physical record now
		// that it has returned, properly accounting for its header size.
		// 当前记录的起始地址
		uint64_t physical_record_offset =
			end_of_buffer_offset_ - buffer_.size() - kHeaderSize - fragment.size();

		// resync模式，这种情况下似乎是要跳到一个record的开头
		// 所以遇到middle的时候继续跳过，遇到last的时候也跳过
		// 但是这是跳过的最后一次
		if (resyncing_) {
			if (record_type == kMiddleType) {
				continue;
			}
			else if (record_type == kLastType) {
				resyncing_ = false;
				continue;
			}
			else {
				resyncing_ = false;
			}
		}

		switch (record_type) {
		case kFullType:
			if (in_fragmented_record) {
				// Handle bug in earlier version of log::Writer where
				// it could emit an empty kFirstType record at the tail end
				// of a block followed by a kFullType or kFirstType record
				// at the begining of the next block.
				// 完整记录，直接读取即可
				if (scratch->empty()) {
					in_fragmented_record = false;
				}
				else {
					ReportCorruption(scratch->size(), "partial record without end(1)");
				}
			}
			prospective_record_offset = physical_record_offset;
			scratch->clear();
			*record = fragment;
			// 对于下一条记录而言，这偏移量就是上条记录的偏移量，
			// 也就是这条记录的偏移量
			last_record_offset_ = prospective_record_offset;
			return true;

		case kFirstType:
			if (in_fragmented_record) {
				// Handle bug in earlier versions of log::Writer where
				// it could emit an empty kFirstType record at the tail end
				// of a block followed by a kFullType or kFirstType record
				// at the beginning of the next block.
				if (scratch->empty()) {
					in_fragmented_record = false;
				}
				else {
					ReportCorruption(scratch->size(), "partial record without end(2)");
				}
			}
			prospective_record_offset = physical_record_offset;
			scratch->assign(fragment.data(), fragment.size());
			in_fragmented_record = true;
			break;

		case kMiddleType:
			if (!in_fragmented_record) {
				ReportCorruption(fragment.size(),
					"missing start of fragmented record(1)");
			}
			else {
				scratch->append(fragment.data(), fragment.size());
			}
			break;

		case kLastType:
			if (!in_fragmented_record) {
				ReportCorruption(fragment.size(),
					"missing start of fragmented record(2)");
			}
			else {
				scratch->append(fragment.data(), fragment.size());
				*record = Slice(*scratch);
				last_record_offset_ = prospective_record_offset;
				return true;
			}
			break;

		case kEof:
			if (in_fragmented_record) {
				// This can be caused by the writer dying immediately after
				// writing a physical record but before completing the next; don't
				// treat it as a corruption, just ignore the entire logical record.
				scratch->clear();
			}
			return false;

		case kBadRecord:
			if (in_fragmented_record) {
				ReportCorruption(scratch->size(), "error in middle of record");
				in_fragmented_record = false;
				scratch->clear();
			}
			break;

		default:
			char buf[40];
			snprintf(buf, sizeof(buf), "unknown record type %u", record_type);
			ReportCorruption(
				(fragment.size() + (in_fragmented_record ? scratch->size() : 0)),
				buf);
			in_fragmented_record = false;
			scratch->clear();
			break;
		}
	}
	return false;
}

uint64_t Reader::LastRecordOffset() {
	return last_record_offset_;
}

void Reader::ReportCorruption(uint64_t bytes, const char* reason) {
	ReportDrop(bytes, Status::Corruption(reason));
}

void Reader::ReportDrop(uint64_t bytes, const Status& reason) {
	if (reporter_ != NULL &&
		end_of_buffer_offset_ - buffer_.size() - bytes >= initial_offset_) {
		reporter_->Corruption(static_cast<size_t>(bytes), reason);
	}
}

unsigned int Reader::ReadPhysicalRecord(Slice* result) {
	while (true) {
		if (buffer_.size() < kHeaderSize) {
			// 因为不是结尾，说明上次读取的是一整个块，现在这个块只剩下补充的0，跳过即可
			if (!eof_) {
				// Last read was a full read, so this is a trailer to skip
				buffer_.clear();
				// 尽可能多的read一个block size(如果余下的数据够的话)
				// 把read出来的内容放到buffer_里面
				Status status = file_->Read(kBlockSize, &buffer_, backing_store_);
				// end_of_buffer_offset_指向buffer之后的第一个位置
				end_of_buffer_offset_ += buffer_.size();
				if (!status.ok()) {
					buffer_.clear();
					ReportDrop(kBlockSize, status);
					eof_ = true;
					return kEof;
				}
				else if (buffer_.size() < kBlockSize) {
					// 当前成功read了之后，如果当前能read的size小于一个block size
					// 则说明已经file已经被read完了
					eof_ = true;
				}
				continue;
			}
			else {
				// Note that if buffer_ is non-empty, we have a truncated header at the
				// end of the file, which can be caused by the writer crashing in the
				// middle of writing the header. Instead of considering this an error,
				// just report EOF.
				buffer_.clear();
				return kEof;
			}
		}

		// Parse the header
		const char* header = buffer_.data();
		const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
		const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
		const unsigned int type = header[6];
		const uint32_t length = a | (b << 8);
		if (kHeaderSize + length > buffer_.size()) {
			// 当前record的长度，包括header的长度+数据的长度大于buffer的size
			size_t drop_size = buffer_.size();
			buffer_.clear();
			if (!eof_) {
				ReportCorruption(drop_size, "bad record length");
				return kBadRecord;
			}
			// If the end of the file has been reached without reading |length| bytes
			// of payload, assume the writer died in the middle of writing the record.
			// Don't report a corruption
			return kEof;
		}

		if (type == kZeroType && length == 0) {
			// Skip zero length record without reporting any drops since
			// such records are produced by the mmap based writing code in
			// env_posix.cc that preallocates file regions.
			buffer_.clear();
			return kBadRecord;
		}

		// Check crc
		if (checksum_) {
			uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(header));
			uint32_t actual_crc = crc32c::Value(header + 6, 1 + length);
			if (actual_crc != expected_crc) {
				// Drop the rest of the buffer since "length" itself may have
				// been corrupted and if we trust it, we could find some 
				// fragment of a real log record that just happens to look
				// like a valid log record.
				size_t drop_size = buffer_.size();
				buffer_.clear();
				ReportCorruption(drop_size, "checksum mismatch");
				return kBadRecord;
			}
		}

		// buffer是一块儿长度，当读取结束一条记录时
		// buffer指向内容的指针向前移动kHeaderSize + length，即下一条记录的起始地址
		buffer_.remove_prefix(kHeaderSize + length);

		// Skip physical record that started before initial_offset_
		if (end_of_buffer_offset_ - buffer_.size() - kHeaderSize - length <
			initial_offset_) {
			result->clear();
			return kBadRecord;
		}


		*result = Slice(header + kHeaderSize, length);
		return type;
	}
}

}  // namespace log
}  // namespace leveldb