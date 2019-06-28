#ifndef STORAGE_LEVELDB_TABLE_FORMAT_H_
#define STORAGE_LEVELDB_TABLE_FORMAT_H_

#include <string>
#include <stdint.h>
#include "slice.h"
#include "status.h"

namespace leveldb {

class Block;
class RandomAccessFile;
struct ReadOptions;

// BlockHandle is a pointer to the extent of a file that stores a data
// block or a meta block.
// 一个指向data block或者meta block的指针
// 它有offset和size两个数据成员
class BlockHandle {
public:
	BlockHandle();

	// The offset of the block in the file
	uint64_t offset() const { return offset_; }
	void set_offset(uint64_t offset) { offset_ = offset; }

	// The size of the stored block
	uint64_t size() const { return size_; }
	void set_size(uint64_t size) { size_ = size; }

	void EncodeTo(std::string* dst) const;
	Status DecodeFrom(Slice* input);

	// Maximum encoding length of a BlockHandle
	enum { kMaxEncodingLength = 10 + 10 };

private:
	uint64_t offset_;
	uint64_t size_;
};

// Footer encapsulates the fixed information stored at the tail
// end of every table file
// 在每个sstable尾部的信息
// 包括metablock handle, index handle和一个magic number
class Footer {
public:
	Footer() { }

	// The block handle for the metaindex block of the table
	const BlockHandle& metaindex_handle() const { return metaindex_handle_; }
	void set_metaindex_handle(const BlockHandle& h) { metaindex_handle_ = h; }

	// The block handle for the index block of the table
	const BlockHandle& index_handle() const {
		return index_handle_;
	}
	void set_index_handle(const BlockHandle& h) {
		index_handle_ = h;
	}

	void EncodeTo(std::string* dst) const;
	Status DecodeFrom(Slice* input);

	// Encoded length of a Footer.  Note that the serialization of a
	// Footer will always occupy exactly this many bytes.  It consists
	// of two block handles and a magic number.
	enum {
		kEncodedLength = 2 * BlockHandle::kMaxEncodingLength + 8
	};

private:
	BlockHandle metaindex_handle_;
	BlockHandle index_handle_;
};

// kTableMagicNumber was picked by running
//    echo http://code.google.com/p/leveldb/ | sha1sum
// and taking the leading 64 bits.
static const uint64_t kTableMagicNumber = 0xdb4775248b80fb57ull;

// 1-byte type + 32-bit crc
static const size_t kBlockTrailerSize = 5;

struct BlockContents {
	Slice data;			  // Actual contents of data
	bool cacheable;		  // True iff data can be cached
	bool heap_allocated;  // True iff called should delete[] data.data()
};

// Read the block identified by "handle" from "file".  On failure
// return non-OK.  On success fill *result and return OK.
extern Status ReadBlock(RandomAccessFile* file,
	const ReadOptions& options,
	const BlockHandle& handle,
	BlockContents* result);

// Implementation details follow.  Clients should ignore,
// 把offset和size全部设置为1，全64位都是1
inline BlockHandle::BlockHandle()
	: offset_(~static_cast<uint64_t>(0)),
	size_(~static_cast<uint64_t>(0)) {
}

}  // namespace leveldb

#endif // !STORAGE_LEVELDB_TABLE_FORMAT_H_
