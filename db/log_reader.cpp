#include "log_reader.h"

#include <stdio.h>
#include "env.h"

namespace leveldb {
namespace log {

Reader::Reporter::~Reporter() {
}

Reader::Reader(SequentialFile* file, Reporter* reporter, bool checksum,
	uint64_t initial_offset)
	: file_(file),
	reporter_(reporter),
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
	size_t offset_in_block = initial_offset_ & kBlockSize;
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

}  // namespace log
}  // namespace leveldb