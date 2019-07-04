#include "two_level_iterator.h"

#include "block.h"
#include "format.h"
#include "iterator_wrapper.h"
#include "options.h"

namespace leveldb {

namespace {

typedef Iterator* (*BlockFunction)(void*, const ReadOptions&, const Slice&);

class TwoLevelIterator : public Iterator {
public:
	TwoLevelIterator(
		Iterator* index_iter,
		BlockFunction block_function,
		void* arg,
		const ReadOptions& options);

	virtual ~TwoLevelIterator();

	virtual void Seek(const Slice& target);
	virtual void SeekToFirst();
	virtual void SeekToLast();
	virtual void Next();
	virtual void Prev();

	virtual bool Valid() const {
		return data_iter_.Valid();
	}
	virtual Slice key() const {
		assert(Valid());
		return data_iter_.key();
	}
	virtual Slice value() const {
		assert(Valid());
		return data_iter_.value();
	}
	virtual Status status() const {
		// It'd be nice if status() returned a const Status& instead of a Status
		if (!index_iter_.status().ok()) {
			return index_iter_.status();
		}
		else if (data_iter_.iter() != NULL && !data_iter_.status().ok()) {
			return data_iter_.status();
		}
		else {
			return status_;
		}
	}

private:
	void SaveError(const Status& s) {
		if (status_.ok() && !s.ok()) status_ = s;
	}
	void SkipEmptyDataBlocksForward();
	void SkipEmptyDataBlocksBackward();
	void SetDataIterator(Iterator* data_iter);
	void InitDataBlock();

	BlockFunction block_function_;
	void* arg_;
	const ReadOptions options_;
	Status status_;
	IteratorWrapper index_iter_;
	IteratorWrapper data_iter_; // May be NULL
	// If data_iter_ is non-NULL, then "data_block_handle_" holds the
	// "index_value" passed to block_function_ to create the data_iter_.
	std::string data_block_handle_;
};

TwoLevelIterator::TwoLevelIterator(
	Iterator* index_iter,
	BlockFunction block_function,
	void* arg,
	const ReadOptions& options)
	: block_function_(block_function),
	arg_(arg),
	options_(options),
	index_iter_(index_iter),
	data_iter_(NULL) {
}

TwoLevelIterator::~TwoLevelIterator() {
}

// Index Block的block_data字段中，每一条记录的key都满足：
// 大于上一个Data Block的所有key，并且小于后面所有Data Block的key
void TwoLevelIterator::Seek(const Slice& target) {
	index_iter_.Seek(target);
	InitDataBlock();
	if (data_iter_.iter() != NULL) data_iter_.Seek(target);
	SkipEmptyDataBlocksForward();
}

void TwoLevelIterator::SeekToFirst() {
	index_iter_.SeekToFirst();
	InitDataBlock();
	if (data_iter_.iter() != NULL) data_iter_.SeekToFirst();
	SkipEmptyDataBlocksForward();
}

void TwoLevelIterator::SeekToLast() {
	index_iter_.SeekToLast();
	InitDataBlock();
	if (data_iter_.iter() != NULL) data_iter_.SeekToLast();
	SkipEmptyDataBlocksBackward();
}

void TwoLevelIterator::Next() {
	assert(Valid());
	data_iter_.Next();
	SkipEmptyDataBlocksForward();
}

void TwoLevelIterator::Prev() {
	assert(Valid());
	data_iter_.Prev();
	SkipEmptyDataBlocksBackward();
}

void TwoLevelIterator::SkipEmptyDataBlocksForward() {
	// 1. 如果data_iter_.iter()为NULL，说明index_iter_.Valid()为NULL时
	// 调用了SetDataIterator(NULL)，此时直接返回
	// 2. 如果data_iter_.Valid()为false，说明当前Data Block的block_data字段
	// 读完了，开始读下一个Data Block的block_data字段
	while (data_iter_.iter() == NULL || !data_iter_.Valid()) {
		// Move to next block
		if (!index_iter_.Valid()) {
			SetDataIterator(NULL);
			return;
		}
		index_iter_.Next();
		InitDataBlock();
		if (data_iter_.iter() != NULL) data_iter_.SeekToFirst();
	}
}

void TwoLevelIterator::SkipEmptyDataBlocksBackward() {
	while (data_iter_.iter() == NULL || !data_iter_.Valid()) {
		// Move to next block
		if (!index_iter_.Valid()) {
			SetDataIterator(NULL);
			return;
		}
		index_iter_.Prev();
		InitDataBlock();
		if (data_iter_.iter() != NULL) data_iter_.SeekToLast();
	}
}

void TwoLevelIterator::SetDataIterator(Iterator* data_iter) {
	if (data_iter_.iter() != NULL) SaveError(data_iter_.status());
	data_iter_.Set(data_iter);
}

void TwoLevelIterator::InitDataBlock() {
	if (!index_iter_.Valid()) {
		// 当index_iter_无效时，让data_iter_也无效
		SetDataIterator(NULL);
	}
	else {
		// index_iter_是Index Block中block_data字段迭代器的代理
		// handle是对应的Data BLock的偏移和该Data Block的block_data字段大小编码后的结果
		Slice handle = index_iter_.value();
		if (data_iter_.iter() != NULL && handle.compare(data_block_handle_) == 0) {
			// data_iter_ is already constructed with this iterator, so
			// no need to change anything
			// 如果data_iter_已经创建了，什么都不用干，可以防止InitDataBlock被多次调用
		}
		else {
			// 创建Data Block中block_data字段的迭代器
			Iterator* iter = (*block_function_)(arg_, options_, handle);
			// 将handle转化为data_block_handle_
			data_block_handle_.assign(handle.data(), handle.size());
			// 将iter传给其代理data_iter_
			SetDataIterator(iter);
		}
	}
}

}  // namespace

Iterator* NewTwoLevelIterator(
	Iterator* index_iter,
	BlockFunction block_function,
	void* arg,
	const ReadOptions& options) {
	return new TwoLevelIterator(index_iter, block_function, arg, options);
}

}  // namespace leveldb