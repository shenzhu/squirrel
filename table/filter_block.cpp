#include "filter_block.h"

#include "filter_policy.h"
#include "coding.h"

namespace leveldb {

// Generate new filter every 2KB of data
static const size_t kFilterBaselg = 11;
static const size_t kFilterBase = 1 << kFilterBaselg;

FilterBlockBuilder::FilterBlockBuilder(const FilterPolicy* policy)
	: policy_(policy) {
}

void FilterBlockBuilder::StartBlock(uint64_t block_offset) {
	// block_filter是data block在sstable中的偏移位置
	// 注意这里是除了之后向下取整，不是取余，得到的是之前filter的数量
	uint64_t filter_index = (block_offset / kFilterBase);
	assert(filter_index >= filter_offsets_.size());
	while (filter_index > filter_offsets_.size()) {
		GenerateFilter();
	}
}

void FilterBlockBuilder::AddKey(const Slice& key) {
	Slice k = key;
	start_.push_back(keys_.size());
	keys_.append(k.data(), k.size());
}

Slice FilterBlockBuilder::Finish() {
	if (!start_.empty()) {
		GenerateFilter();
	}

	// Append array of per-filter offsets
	// array_offset指的是
	const uint32_t array_offset = result_.size();
	// result中本来是存储着所有的filter，现在把所有的filter的偏移值依次
	// append进result中
	for (size_t i = 0; i < filter_offsets_.size(); i++) {
		PutFixed32(&result_, filter_offsets_[i]);
	}

	// 这里将这个filter block包含的所有filter的总字节数(array_offset)拼接到result中去
	// 通过它可以找到offset数组在filter block中的偏移位置
	// 最后把kFilterBaseLg参数拼接进去，前面我们经常看到的2kb就是通过这个参数计算出来的
	// 通过改变这个参数可以调节每个filter对应的block offset区间
	PutFixed32(&result_, array_offset);
	result_.push_back(kFilterBaselg);  // Save encoding parameters
	return Slice(result_);
}

void FilterBlockBuilder::GenerateFilter() {
	const size_t num_keys = start_.size();
	if (num_keys == 0) {
		// Fast path if there are no keys for this filter
		filter_offsets_.push_back(result_.size());
		return;
	}

	// Make list of keys from flattened key structure
	// 下面这段儿代码就是把所有的key都copy到了tmp_keys_里面
	start_.push_back(keys_.size());  // Simplify length computation
	tmp_keys_.resize(num_keys);
	for (size_t i = 0; i < num_keys; i++) {
		const char* base = keys_.data() + start_[i];
		size_t length = start_[i + 1] - start_[i];
		tmp_keys_[i] = Slice(base, length);
	}

	// Generate filter for current set of keys and append to result_.
	filter_offsets_.push_back(result_.size());
	policy_->CreateFilter(&tmp_keys_[0], static_cast<int>(num_keys), &result_);

	tmp_keys_.clear();
	keys_.clear();
	start_.clear();
}

FilterBlockReader::FilterBlockReader(const FilterPolicy* policy,
	const Slice& contents)
	: policy_(policy),
	data_(NULL),
	offset_(NULL),
	num_(0),
	base_lg_(0) {
	size_t n = contents.size();
	if (n < 5) return;  // 1 byte for base_lg_ and 4 for start of offset array
	base_lg_ = contents[n - 1];
	// 这个last_word计算的是filter offset array的偏移量
	uint32_t last_word = DecodeFixed32(contents.data() + n - 5);
	if (last_word > n - 5) return;
	data_ = contents.data();
	offset_ = data_ + last_word;
	// 计算filter的个数
	num_ = (n - 5 - last_word) / 4;
}

bool FilterBlockReader::KeyMayMatch(uint64_t block_offset, const Slice& key) {
	uint64_t index = block_offset >> base_lg_;
	if (index < num_) {
		uint32_t start = DecodeFixed32(offset_ + index * 4);
		uint32_t limit = DecodeFixed32(offset_ + index * 4 + 4);
		if (start <= limit && limit <= static_cast<size_t>(offset_ - data_)) {
			Slice filter = Slice(data_ + start, limit - start);
			return policy_->KeyMayMatch(key, filter);
		}
		else if (start == limit) {
			// Empty filters do not match any keys
			return false;
		}
	}
	return true;  // Errors are treated as poential matches
}

}  // namespace leveldb