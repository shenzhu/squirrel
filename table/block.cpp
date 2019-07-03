#include "block.h"

#include <vector>
#include <algorithm>
#include "comparator.h"
#include "format.h"
#include "coding.h"
#include "logging.h"

namespace leveldb {

inline uint32_t Block::NumRestarts() const {
	// size_为何要大于sizeof(uint32_t)，因为如果只调用BlockBuilder中
	// 的Finish函数，那么block data至少会包含一个uint32_t类型的重启点
	// 个数信息
	assert(size_ >= sizeof(uint32_t));
	// block data的最后一个uint32_t类型字段表示重启点的个数
	return DecodeFixed32(data_ + size_ - sizeof(uint32_t));
}

Block::Block(const BlockContents& contents)
	: data_(contents.data.data()),
	size_(contents.data.size()),
	owned_(contents.heap_allocated) {
	if (size_ < sizeof(uint32_t)) {
		size_ = 0;  // Error marker
	}
	else {
		size_t max_restarts_allowed = (size_ - sizeof(uint32_t)) / sizeof(uint32_t);
		if (NumRestarts() > max_restarts_allowed) {
			// The size is too small for NumRestarts()
			size_ = 0;
		}
		else {
			// block data最后的地址，减去重启点个数的大小和所有重启点的大小
			restart_offset_ = size_ - (1 + NumRestarts()) * sizeof(uint32_t);
		}
	}
}

Block::~Block() {
	if (owned_) {
		delete[] data_;
	}
}

// Helper routine: decode the next block entry starting at "p",
// storing the number of shared key bytes, non_shared key bytes,
// and the length of the value in "*shared", "*non_shared", and
// "*value_length", respectively.  Will not dereference past "limit".
//
// If any errors are detected, returns NULL.  Otherwise, returns a
// pointer to the key delta (just past the three decoded values).
// 解析从block data的p位置开始的数据，将解析得到的shared、non_shared和value length
// 分别放到"*shared"、"*non_shared"和"*value_length"
// 从源码看出，p应该是一条记录的起始位置
// 如果解析错误，返回NULL。否则，返回指向一条记录的key_delta字段的指针
static inline const char* DecodeEntry(const char* p, const char* limit,
	uint32_t* shared,
	uint32_t* non_shared,
	uint32_t* value_length) {
	if (limit - p < 3) return NULL;
	*shared = reinterpret_cast<const unsigned char*>(p)[0];
	*non_shared = reinterpret_cast<const unsigned char*>(p)[1];
	*value_length = reinterpret_cast<const unsigned char*>(p)[2];
	// 因为采用的编码方式是varint coding
	// 所以如果最高位都是0, 那么按照压缩规则，每个值只占一个字节，且小于128
	// 这里相当于做了一个优化，如果三个值之和都小于128，那肯定是每个值只占一个字节 
	if ((*shared | *non_shared | *value_length) < 128) {
		p += 3;
	}
	else {
		if ((p = GetVarint32Ptr(p, limit, shared)) == NULL) return NULL;
		if ((p = GetVarint32Ptr(p, limit, non_shared)) == NULL) return NULL;
		if ((p = GetVarint32Ptr(p, limit, value_length)) == NULL) return NULL;
	}

	if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)) {
		return NULL;
	}
	return p;
}

class Block::Iter : public Iterator {
private:
	const Comparator* const comparator_;
	// underlaying block contents
	const char* const data_;
	// Offset of restart array (list of fixed32)
	// 重启点信息在block data中的偏移
	uint32_t const restarts_;
	// Number of uint32_t entries in restart array
	// 重启点个数
	uint32_t const num_restarts_;

	// current_ is offset in data_ of current entry.  >= restarts_ if !Valid
	// current_是当前记录在block data中的偏移
	// 如果current_ >= restarts_说明出错
	uint32_t current_;
	// Index of restart block in which current_ falls
	// 重启点的索引
	uint32_t restart_index_;
	std::string key_;
	Slice value_;
	Status status_;

	inline int Compare(const Slice& a, const Slice& b) const {
		return comparator_->Compare(a, b);
	}

	// Return the offset in data_ just past the end of the current entry.
	// 因为value_是一条记录的最后一个字段, 所以这里返回的是下一条记录的偏移量,
	// 也就是current_
	// 但是如果在该函数之前调用了SeekToRestartPoint,
	// 此时的value_.data()=data_,value.size=0
	inline uint32_t NextEntryOffset() const {
		return (value_.data() + value_.size()) - data_;
	}

	// 获取第index个重启点的偏移
	uint32_t GetRestartPoint(uint32_t index) {
		assert(index < num_restarts_);
		return DecodeFixed32(data_ + restarts_ + index * sizeof(uint32_t));
	}

	// 需要注意的是，这里的value_并不是记录中的value字段
	// 而只是一个指向记录起始位置的0长度指针
	// 这样后面的ParseNextKey函数将会解析出重启点的value字段, 并赋值到value_中
	void SeekToRestartPoint(uint32_t index) {
		key_.clear();
		restart_index_ = index;
		// current_ will be fixed by ParseNextKey();

		// ParseNextKey() starts at the end of value_, so set value_ accordingly
		uint32_t offset = GetRestartPoint(index);
		value_ = Slice(data_ + offset, 0);
	}

public:
	Iter(const Comparator* comparator,
		const char* data,
		uint32_t restarts,
		uint32_t num_restarts)
		: comparator_(comparator),
		data_(data),
		restarts_(restarts),
		num_restarts_(num_restarts),
		current_(restarts_),
		restart_index_(num_restarts_) {
		assert(num_restarts_ > 0);
	}

	virtual bool Valid() const { return current_ < restarts_; }
	virtual Status status() const { return status_; }
	virtual Slice key() const {
		assert(Valid());
		return key_;
	}
	virtual Slice value() const {
		assert(Valid());
		return value_;
	}

	virtual void Next() {
		assert(Valid());
		ParseNextKey();
	}

	// 向前解析
	// 1. 先向前查找当前记录之前的重启点
	// 2. 如果当循环到了第一个重启点，其偏移量(0)依然与当前记录的偏移量相等
	//    说明当前记录就是第一条记录，此时初始化current_和restart_index_并返回
	// 3. 调用SeekToRestartPoint定位到符合要求的启动点
	// 4. 向后循环解析，直到解析了原记录之前的一条记录
	virtual void Prev() {
		assert(Valid());

		// Scan backwards to a restart point before current_
		const uint32_t original = current_;
		while (GetRestartPoint(restart_index_) >= original) {
			if (restart_index_ == 0) {
				// No more entries
				current_ = restarts_;
				restart_index_ = num_restarts_;
				return;
			}
			restart_index_--;
		}

		SeekToRestartPoint(restart_index_);
		do {
			// Loop until end of current entry hits the start of original entry
		} while (ParseNextKey() && NextEntryOffset() < original);
	}

	// 从左到右（从前到后）查找第一条key大于target的记录
	// 1.二分查找，找到key < target的最后一个重启点
	// 2.定位到该重启点，其索引由left指定，这是前面二分查找到的结果。如前面所分析的，
	//   value_指向重启点的地址，而size_指定为0，这样ParseNextKey函数将会解析出重启点key和value。
	// 3.自重启点线性向下查找，直到遇到key>=target的记录或者直到最后一条记录，也不满足key>=target，返回
	virtual void Seek(const Slice& target) {
		// Binary search in restart array to find the last restart point
		// with a key < target
		uint32_t left = 0;
		uint32_t right = num_restarts_ - 1;
		while (left < right) {
			uint32_t mid = (left + right + 1) / 2;
			uint32_t region_offset = GetRestartPoint(mid);
			uint32_t shared, non_shared, value_length;
			const char* key_ptr = DecodeEntry(data_ + region_offset,
				data_ + restarts_,
				&shared, &non_shared, &value_length);
			if (key_ptr == NULL || (shared != 0)) {
				CorruptionError();
				return;
			}
			Slice mid_key(key_ptr, non_shared);
			if (Compare(mid_key, target) < 0) {
				// Key at "mid" is smaller than "target".  Therefore all
				// blocks before "mid" are uninteresting.
				left = mid;
			}
			else {
				// Key at "mid" is >= "target".  Therefore all blocks at or
				// after "mid" are uninteresting.
				right = mid - 1;
			}
		}

		// Linear search (within restart block) for first key >= target
		SeekToRestartPoint(left);
		while (true) {
			if (!ParseNextKey()) {
				return;
			}
			if (Compare(key_, target) >= 0) {
				return;
			}
		}
	}

	virtual void SeekToFirst() {
		SeekToRestartPoint(0);
		ParseNextKey();
	}

	virtual void SeekToLast() {
		SeekToRestartPoint(num_restarts_ - 1);
		while (ParseNextKey() && NextEntryOffset() < restarts_) {
			// Keep skipping
		}
	}

private:
	void CorruptionError() {
		current_ = restarts_;
		restart_index_ = num_restarts_;
		status_ = Status::Corruption("bad entry in block");
		key_.clear();
		value_.clear();
	}

	bool ParseNextKey() {
		current_ = NextEntryOffset();
		// 指向当前记录
		const char* p = data_ + current_;
		// limit限制了记录存储区的范围
		const char* limit = data_ + restarts_;
		if (p >= limit) {
			// No more entries to return. Mark as invalid
			// 如果出错，恢复到默认值，并返回false
			current_ = restarts_;
			restart_index_ = num_restarts_;
			return false;
		}

		// Decode next entry
		uint32_t shared, non_shard, value_length;
		p = DecodeEntry(p, limit, &shared, &non_shard, &value_length);
		if (p == NULL || key_.size() < shared) {
			CorruptionError();
			return false;
		}
		else {
			key_.resize(shared);
			key_.append(p, non_shard);
			value_ = Slice(p + non_shard, value_length);
			while (restart_index_ + 1 < num_restarts_ &&
				GetRestartPoint(restart_index_ + 1) < current_) {
				++restart_index_;
			}
			return true;
		}
	}
};

Iterator* Block::NewIterator(const Comparator* cmp) {
	if (size_ < sizeof(uint32_t)) {
		return NewErrorIterator(Status::Corruption("bad block contents"));
	}
	const uint32_t num_restarts = NumRestarts();
	if (num_restarts == 0) {
		return NewEmptyIterator();
	}
	else {
		return new Iter(cmp, data_, restart_offset_, num_restarts);
	}
}

}  // namespace leveldb