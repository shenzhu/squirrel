#include "memtable.h"
#include "dbformat.h"
#include "comparator.h"
#include "env.h"
#include "iterator.h"
#include "coding.h"

namespace leveldb {

static Slice GetLengthPrefixedSlice(const char* data) {
	uint32_t len;
	const char* p = data;
	p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
	return Slice(p, len);
}

MemTable::MemTable(const InternalKeyComparator& cmp)
	: comparator_(cmp),
	refs_(0),
	table_(comparator_, &arena_) {
}

MemTable::~MemTable() {
	assert(refs_ == 0);
}

size_t MemTable::ApproximateMemoryUsage() { return arena_.MemoryUsage(); }

int MemTable::KeyComparator::operator()(const char* aptr, const char* bptr) const {
	// Internal keys are encoded as length-prefixed strings.
	Slice a = GetLengthPrefixedSlice(aptr);
	Slice b = GetLengthPrefixedSlice(bptr);
	return comparator.Compare(a, b);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
static const char* EncodeKey(std::string* scratch, const Slice& target) {
	scratch->clear();
	PutVarint32(scratch, target.size());
	scratch->append(target.data(), target.size());
	return scratch->data();
}

class MemTableIterator : public Iterator {
public:
	// 需要注意的是MemTable::Table就是SkipList<const char*, KeyComparator>
	// MemTableIterator构造函数需要跳跃表的指针，毕竟遍历MemTable等同于遍历SkipList
	explicit MemTableIterator(MemTable::Table* table) : iter_(table) { }

	virtual bool Valid() const { return iter_.Valid(); }
	virtual void Seek(const Slice& k) { iter_.Seek(EncodeKey(&tmp_, k)); }
	virtual void SeekToFirst() { iter_.SeekToFirst(); }
	virtual void SeekToLast() { iter_.SeekToLast(); }
	virtual void Next() { iter_.Next(); }
	virtual void Prev() { iter_.Prev(); }
	virtual Slice key() const { return GetLengthPrefixedSlice(iter_.key()); }
	virtual Slice value() const {
		// 在内存中跳过键的部分后面就是值
		// 因为在调用GetVarint32Ptr之后p会直接advance到读取的数据之后
		Slice key_slice = GetLengthPrefixedSlice(iter_.key());
		return GetLengthPrefixedSlice(key_slice.data() + key_slice.size());
	}

	virtual Status status() const { return Status::OK(); }

private:
	MemTable::Table::Iterator iter_;
	std::string tmp_;         // For passing to EncodeKey

	// No copying allowed
	MemTableIterator(const MemTableIterator&);
	void operator=(const MemTableIterator&);
};

Iterator* MemTable::NewIterator() {
	return new MemTableIterator(&table_);
}

void MemTable::Add(SequenceNumber s, ValueType type,
	const Slice& key,
	const Slice& value) {
	// Format of an entry is concatenation of:
	//  key_size     : varint32 of internal_key.size()
	//  key bytes    : char[internal_key.size()]
	//  value_size   : varint32 of value.size()
	//  value bytes  : char[value.size()]
	// 在SkipList存储的内容是把用户的键和值进行编码后的值，下面就是具体实现编码的部分
	size_t key_size = key.size();
	size_t val_size = value.size();
	// InternalKey的长度是用户指定键 + (顺序ID << 8 | 值类型)
	// 所以长度是用户指定键的长度 + 8
	size_t internal_key_size = key_size + 8;
	// 编码后的数据[InternalKey长度(Varint32)][InternalKey][value长度(Varint32)][value]
	// 所以整体编码后需要的内存长度就下面的算法
	const size_t encoded_len =
		VarintLength(internal_key_size) + internal_key_size +
		VarintLength(val_size) + val_size;
	
	// 申请需要的内存
	char* buf = arena_.Allocate(encoded_len);
	char* p = EncodeVarint32(buf, internal_key_size);
	
	// 存放internal key
	memcpy(p, key.data(), key_size);
	p += key_size;
	EncodeFixed64(p, (s << 8) | type);
	p += 8;

	// 存放value
	p = EncodeVarint32(p, val_size);
	memcpy(p, value.data(), val_size);

	assert((p + val_size) - buf == encoded_len);
	table_.Insert(buf);
}

bool MemTable::Get(const LookupKey& key, std::string* value, Status* s) {
	Slice memkey = key.memtable_key();
	Table::Iterator iter(&table_);
	iter.Seek(memkey.data());
	if (iter.Valid()) {
		// entry format is:
		//    klength  varint32
		//    userkey  char[klength]
		//    tag      uint64
		//    vlength  varint32
		//    value    char[vlength]
		// Check that it belongs to same user key.  We do not check the
		// sequence number since the Seek() call above should have skipped
		// all entries with overly large sequence numbers.
		const char* entry = iter.key();
		uint32_t key_length;
		const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
		if (comparator_.comparator.user_comparator()->Compare(
			Slice(key_ptr, key_length - 8),
			key.user_key()) == 0) {
			// Correct user key
			const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
			switch (static_cast<ValueType>(tag & 0xff)) {
			case kTypeValue: {
				Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
				value->assign(v.data(), v.size());
				return true;
			}
			case kTypeDeletion:
				*s = Status::NotFound(Slice());
				return true;
			}
		}
	}
	return false;
}

}  // namespace leveldb