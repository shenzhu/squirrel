﻿#include "table_cache.h"

#include "filename.h"
#include "env.h"
#include "table.h"
#include "coding.h"

namespace leveldb {

struct TableAndFile {
	RandomAccessFile* file;
	Table* table;
};

static void DeleteEntry(const Slice& key, void* value) {
	TableAndFile* tf = reinterpret_cast<TableAndFile*>(value);
	delete tf->table;
	delete tf->file;
	delete tf;
}

static void UnrefEntry(void* arg1, void* arg2) {
	Cache* cache = reinterpret_cast<Cache*>(arg1);
	Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
	cache->Release(h);
}

TableCache::TableCache(const std::string& dbname,
	const Options* options,
	int entries)
	: env_(options->env),
	dbname_(dbname),
	options_(options),
	cache_(NewLRUCache(entries)) {
}

TableCache::~TableCache() {
	delete cache_;
}

Status TableCache::FindTable(uint64_t file_number, uint64_t file_size,
	Cache::Handle** handle) {
	Status s;

	// 将file_number写入key里面，转化成Slice
	char buf[sizeof(file_number)];
	EncodeFixed64(buf, file_number);
	Slice key(buf, sizeof(buf));

	*handle = cache_->Lookup(key);
	if (*handle == NULL) {
		// 通过RandomAccessFile创建sstable并用Table打开这个文件
		std::string fname = TableFileName(dbname_, file_number);
		RandomAccessFile* file = NULL;
		Table* table = NULL;
		s = env_->NewRandomAccessFile(fname, &file);
		if (!s.ok()) {
			// 如果创建RandomAccess文件没有成功
			std::string old_fname = SSTTableFileName(dbname_, file_number);
			if (env_->NewRandomAccessFile(old_fname, &file).ok()) {
				s = Status::OK();
			}
		}
		if (s.ok()) {
			s = Table::Open(*options_, file, file_size, &table);
		}

		if (!s.ok()) {
			assert(table == NULL);
			delete file;
			// We do not cache error results so that if the error is transient,
			// or somebody repairs the file, we recover automatically.
		}
		else {
			TableAndFile* tf = new TableAndFile;
			tf->file = file;
			tf->table = table;
			*handle = cache_->Insert(key, tf, 1, &DeleteEntry);
		}
	}
	return s;
}

Iterator* TableCache::NewIterator(const ReadOptions& options,
	uint64_t file_number,
	uint64_t file_size,
	Table** tableptr) {
	if (tableptr != NULL) {
		*tableptr = NULL;
	}

	Cache::Handle* handle = NULL;
	Status s = FindTable(file_number, file_size, &handle);
	if (!s.ok()) {
		return NewErrorIterator(s);
	}

	Table* table = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
	Iterator* result = table->NewIterator(options);
	result->RegisterCleanup(&UnrefEntry, cache_, handle);
	if (tableptr != NULL) {
		*tableptr = table;
	}
	return result;
}

Status TableCache::Get(const ReadOptions& options,
	uint64_t file_number,
	uint64_t file_size,
	const Slice& k,
	void* arg,
	void (*saver)(void*, const Slice&, const Slice&)) {
	Cache::Handle* handle = NULL;
	Status s = FindTable(file_number, file_size, &handle);
	if (s.ok()) {
		Table* t = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
		s = t->InternalGet(options, k, arg, saver);
		cache_->Release(handle);
	}
	return s;
}

}  // namespace leveldb