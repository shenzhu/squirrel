#ifndef STORAGE_LEVELDB_DB_MEMTABLE_H_
#define STORAGE_LEVELDB_DB_MEMTABLE_H_

#include <string>
#include "dbformat.h"
#include "skiplist.h"
#include "arena.h"
#include "iterator.h"

namespace leveldb {

class InternalKeyComparator;
class Mutex;
class MemTableIterator;

class MemTable {
public:
	// MemTables are reference counted.  The initial reference count
	// is zero and the caller must call Ref() at least once.
	// 构造函数，需要提供InternalKeyComparator的对象
	// 这表明在MemTable中是通过InternalKey进行排序的
	explicit MemTable(const InternalKeyComparator& comparator);

	// Increase reference count
	void Ref() { ++refs_; }

	// Drop reference count.  Delete if no more references exist.
	void Unref() {
		--refs_;
		assert(refs_ >= 0);
		if (refs_ <= 0) {
			delete this;
		}
	}

	// Returns an estimate of the number of bytes of data in use by this
	// data structure. It is safe to call when MemTable is being modified.
	size_t ApproximateMemoryUsage();

	// Return an iterator that yields the contents of the memtable.
	//
	// The caller must ensure that the underlying MemTable remains live
	// while the returned iterator is live.  The keys returned by this
	// iterator are internal keys encoded by AppendInternalKey in the
	// db/format.{h,cc} module.
	Iterator* NewIterator();

	// Add an entry into memtable that maps key to value at the
	// specified sequence number and with the specified type.
	// Typically value will be empty if type==kTypeDeletion.
	// 
    // 向MemTable中添加对象，提供提供了用户指定的键和值，同时还提供了顺序号和值类型
	// 说明顺序号是上级(leveldb)别产生的
	// 如果是删除操作，value应该没有任何值
	void Add(SequenceNumber seq, ValueType type,
		const Slice& key,
		const Slice& value);

	// If memtable contains a value for key, store it in *value and return true.
	// If memtable contains a deletion for key, store a NotFound() error
	// in *status and return true.
	// Else, return false.
	bool Get(const LookupKey& key, std::string* value, Status* s);

private:
	~MemTable();  // Private since only Unref() should be used to delete it
	
	// 自定义了比较器，说明在InternalKey基础上又进行了扩展
	// 但最终还是通过InternalKeyComparator实现的比较
	struct KeyComparator {
		const InternalKeyComparator comparator;
		explicit KeyComparator(const InternalKeyComparator& c) : comparator(c) { }
		// 这里可以看出来进行比较的已经不是Slice
		// 而是一个buf，所以需要比较器解析buf
		int operator()(const char* a, const char* b) const;
	};
	friend class MemTableIterator;
	friend class MemTableBackwardIterator;

	typedef SkipList<const char*, KeyComparator> Table;

	// 成员变量包括：比较器，引用计数，内存管理和跳跃表
	KeyComparator comparator_;
	int refs_;
	Arena arena_;
	Table table_;

	// No copying allowed
	MemTable(const MemTable&);
	void operator=(const MemTable&);
};

}  // namespace leveldb

#endif // !STORAGE_LEVELDB_DB_MEMTABLE_H_
