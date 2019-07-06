#include "table_builder.h"

#include <assert.h>
#include "comparator.h"
#include "env.h"
#include "filter_policy.h"
#include "options.h"
#include "block_builder.h"
#include "filter_block.h"
#include "format.h"
#include "coding.h"
#include "crc32c.h"
#include "port.h"

namespace leveldb {

struct TableBuilder::Rep {
	Options options;  // data block的选项
	Options index_block_options;  // index block的选项
	WritableFile* file;  // sstable文件
	uint64_t offset;  // 要写入data block在sstable文件中的偏移，初始为0
	Status status;  // 当前状态，初始为ok
	BlockBuilder data_block;  // 当前操作的data block
	BlockBuilder index_block;  // sstable的index block
	std::string last_key;  // 当前data block最后的k/v对的key
	int64_t num_entries;  // 当前data block的个数
	bool closed;		  // Either Finish() or Abandon() has been called
	// 根据filter数据快速定位key是否在block中
	// 存储的过滤器信息，它会存储(key, 对应的data block在sstable的偏移值)，不一定是
	// 完全精确的，以快速定位
	FilterBlockBuilder* filter_block;

	// We do not emit the index entry for a block until we have seen the
	// first key for the next data block.  This allows us to use shorter
	// keys in the index block.  For example, consider a block boundary
	// between the keys "the quick brown fox" and "the who".  We can use
	// "the r" as the key for the index block entry since it is >= all
	// entries in the first block and < all entries in subsequent
	// blocks.
	//
	// Invariant: r->pending_index_entry is true only if data_block is empty.
	bool pending_index_entry;
	// Handle to add to index block
	// 添加到index block的data block的信息
	BlockHandle pending_handle;

	std::string compressed_output;  // 压缩后的data block，临时存储，写入后即被清空

	Rep(const Options& opt, WritableFile* f)
		: options(opt),
		index_block_options(opt),
		file(f),
		offset(0),
		data_block(&options),
		index_block(&index_block_options),
		num_entries(0),
		closed(false),
		filter_block(opt.filter_policy == NULL ? NULL
			: new FilterBlockBuilder(opt.filter_policy)),
		pending_index_entry(false) {
		index_block_options.block_restart_interval = 1;
	}
};

TableBuilder::TableBuilder(const Options& options, WritableFile* file)
	: rep_(new Rep(options, file)) {
	if (rep_->filter_block != NULL) {
		rep_->filter_block->StartBlock(0);
	}
}

TableBuilder::~TableBuilder() {
	assert(rep_->closed);  // Catch errors where caller forgot to call Finish()
	delete rep_->filter_block;
	delete rep_;
}

Status TableBuilder::ChangeOptions(const Options& options) {
	// Note: if more fields are added to Options, update
	// this function to catch changes that should not be allowed to
	// change in the middle of building a Table.
	if (options.comparator != rep_->options.comparator) {
		return Status::InvalidArgument("changing comparator while building table");
	}

	// Note that any live BlockBuilders point to rep_->options and therefore
	// will automatically pick up the updated options.
	rep_->options = options;
	rep_->index_block_options = options;
	rep_->index_block_options.block_restart_interval = 1;
	return Status::OK();
}

void TableBuilder::Add(const Slice& key, const Slice& value) {
	Rep* r = rep_;
	assert(!r->closed);
	if (!ok()) return;

	// 如果当前的record数量不为0的话，要检测一下即将插入的key和last key的关系
	if (r->num_entries > 0) {
		assert(r->options.comparator->Compare(key, Slice(r->last_key)) > 0);
	}

	// 如果标记pending_index_entry为true，表明遇到下一个data block的第一个k/v，根据key
	// 调整last_key，这是通过Comparator的FindShortestSeparator完成的
	// 接下来将pending_handle加入到index block中
	// 最后将pending_index_entry设置为false
	// 这个标记的意义: 直到遇到下一个data block的第一个key时，我们才为上一个data block生成
	// index entry，这样的好处是可以为index使用较短的key，比如上一个data block最后一个k/v
	// 的key是"the quick brown fox"，其后继data block的第一个key是"the who"，我们就
	// 可以用一个较短的字符串"the r"作为上一个data block的index block entry的key
	// 简而言之，就是在开始下一个datablock时，Leveldb才将上一个data block加入到index block中
	// 标记pending_index_entry就是干这个用的
	// 对应data block的index entry信息就保存在(BlockHandle)pending_handle
	if (r->pending_index_entry) {
		assert(r->data_block.empty());
		r->options.comparator->FindShortestSeparator(&r->last_key, key);
		std::string handle_encoding;
		r->pending_handle.EncodeTo(&handle_encoding);
		r->index_block.Add(r->last_key, Slice(handle_encoding));
		r->pending_index_entry;
	}

	if (r->filter_block != NULL) {
		r->filter_block->AddKey(key);
	}

	r->last_key.assign(key.data(), key.size());
	r->num_entries++;
	r->data_block.Add(key, value);

	const size_t estimated_block_size = r->data_block.CurrentSizeEstimate();
	if (estimated_block_size >= r->options.block_size) {
		Flush();
	}
}

void TableBuilder::Flush() {
	Rep* r = rep_;
	assert(!r->closed);
	if (!ok()) return;
	if (r->data_block.empty()) return;
	// 保证pending_index_entry为false，即data block的Add已经完成
	assert(!r->pending_index_entry);
	// 写入data block，并设置其index entry信息
	WriteBlock(&r->data_block, &r->pending_handle);
	// 写入成功，则Flush文件，并设置r->pending_index_entry为true，
	// 以根据下一个data block的first key调整index entry的key—即r->last_key
	if (ok()) {
		r->pending_index_entry = true;
		r->status = r->file->Flush();
	}
	if (r->filter_block != NULL) {
		// 将data block在sstable中的偏移加入到filter block中
		r->filter_block->StartBlock(r->offset);
	}
}

void TableBuilder::WriteBlock(BlockBuilder* block, BlockHandle* handle) {
	// File format contains a sequence of blocks where each block has:
	//    block_data: uint8[n]
	//    type: uint8
	//    crc: uint32
	assert(ok());
	Rep* r = rep_;
	// 获得data block的序列化字符串
	Slice raw = block->Finish();

	Slice block_contents;
	CompressionType type = r->options.compression;
	// TODO(postrelease): Support more compression options: zlib?
	switch (type) {
	case kNoCompression:
		block_contents = raw;
		break;

	case kSnappyCompression: {
		std::string* compressed = &r->compressed_output;
		if (port::Snappy_Compress(raw.data(), raw.size(), compressed) &&
			compressed->size() < raw.size() - (raw.size() / 8u)) {
			block_contents = *compressed;
		}
		else {
			// Snappy not supported, or compressed less than 12.5%, so just
			// store uncompressed form
			block_contents = raw;
			type = kNoCompression;
		}
		break;
	}
	}

	// 将data内容写入到文件，并充值block成为初始化状态，清空compressed ouput
	WriteRawBlock(block_contents, type, handle);
	r->compressed_output.clear();
	block->Reset();
}

void TableBuilder::WriteRawBlock(const Slice& block_contents,
	CompressionType type,
	BlockHandle* handle) {
	Rep* r = rep_;
	handle->set_offset(r->offset);
	handle->set_size(block_contents.size());
	// 写入data block的内容
	r->status = r->file->Append(block_contents);
	if (r->status.ok()) {
		char trailer[kBlockTrailerSize];
		trailer[0] = type;
		uint32_t crc = crc32c::Value(block_contents.data(), block_contents.size());
		crc = crc32c::Extend(crc, trailer, 1);  // Extend crc to cover block type
		EncodeFixed32(trailer + 1, crc32c::Mask(crc));
		r->status = r->file->Append(Slice(trailer, kBlockTrailerSize));
		if (r->status.ok()) {
			// 写入成功更新offset-下一个data block的写入偏移
			r->offset += block_contents.size() + kBlockTrailerSize;
		}
	}
}

Status TableBuilder::status() const {
	return rep_->status;
}

// 将所有已经添加的k/v对持久化到sstable，并关闭sstable文件
Status TableBuilder::Finish() {
	Rep* r = rep_;
	Flush();
	assert(!r->closed);
	r->closed = true;

	BlockHandle filter_block_handle, metaindex_block_handle, index_block_handle;

	// Write filter block
	// 这块儿是真是写入数据
	if (ok() && r->filter_block != NULL) {
		WriteRawBlock(r->filter_block->Finish(), kNoCompression,
			&filter_block_handle);
	}

	// Write metaindex block
	// 这部分是写入index
	if (ok()) {
		BlockBuilder meta_index_block(&r->options);
		if (r->filter_block != NULL) {
			// Add mapping from "filter.Name" to location of filter data
			std::string key = "filter.";
			key.append(r->options.filter_policy->Name());
			std::string handle_encoding;
			filter_block_handle.EncodeTo(&handle_encoding);
			meta_index_block.Add(key, handle_encoding);
		}

		// TODO(postrelease): Add stats and other meta blocks
		WriteBlock(&meta_index_block, &metaindex_block_handle);
	}

	// Write index block
	if (ok()) {
		if (r->pending_index_entry) {
			r->options.comparator->FindShortSuccessor(&r->last_key);
			std::string handle_encoding;
			r->pending_handle.EncodeTo(&handle_encoding);
			r->index_block.Add(r->last_key, Slice(handle_encoding));
			r->pending_index_entry = false;
		}
		WriteBlock(&r->index_block, &index_block_handle);
	}

	// Write footer
	if (ok()) {
		Footer footer;
		footer.set_metaindex_handle(metaindex_block_handle);
		footer.set_index_handle(index_block_handle);
		std::string footer_encoding;
		footer.EncodeTo(&footer_encoding);
		r->status = r->file->Append(footer_encoding);
		if (r->status.ok()) {
			r->offset += footer_encoding.size();
		}
	}
	return r->status;
}

void TableBuilder::Abandon() {
	Rep* r = rep_;
	assert(!r->closed);
	r->closed = true;
}

uint64_t TableBuilder::NumEntries() const {
	return rep_->num_entries;
}

uint64_t TableBuilder::FileSize() const {
	return rep_->offset;
}

}  // namespace leveldb