// A database can be configured with a custom FilterPolicy object.
// This object is responsible for creating a small filter from a set
// of keys.  These filters are stored in leveldb and are consulted
// automatically by leveldb to decide whether or not to read some
// information from disk. In many cases, a filter can cut down the
// number of disk seeks form a handful to a single disk seek per
// DB::Get() call.
//
// Most people will want to use the builtin bloom filter support (see
// NewBloomFilterPolicy() below).

#ifndef STORAGE_LEVELDB_INCLUDE_FILTER_POLICY_H_
#define STORAGE_LEVELDB_INCLUDE_FILTER_POLICY_H_

#include <string>

namespace leveldb {

class Slice;

class FilterPolicy {
public:
	virtual ~FilterPolicy();

	// Return the name of this policy.  Note that if the filter encoding
	// changes in an incompatible way, the name returned by this method
	// must be changed.  Otherwise, old incompatible filters may be
	// passed to methods of this type.
	virtual const char* Name() const = 0;

	// keys[0,n-1] contains a list of keys (potentially with duplicates)
	// that are ordered according to the user supplied comparator
	// Append a filter that summarizes keys[0,n-1] to *dst.
	//
	// Warning: do not change the initial contents of *dst. Instead,
	// append the newly constructed filter to *dst
	virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const = 0;

	// "filter" contains the data appended by a preceding call to
	// CreateFilter() on this class.  This method must return true if
	// the key was in the list of keys passed to CreateFilter().
	// This method may return true or false if the key was not on the
	// list, but it should aim to return false with a high probability.
	virtual bool KeyMayMatch(const Slice& key, const Slice& filter) const = 0;
};

// Note: if you are using a custom comparator that ignores some parts
// of the keys being compared, you must not use NewBloomFilterPolicy()
// and must provide your own FilterPolicy that also ignores the
// corresponding parts of the keys.  For example, if the comparator
// ignores trailing spaces, it would be incorrect to use a
// FilterPolicy (like NewBloomFilterPolicy) that does not ignore
// trailing spaces in keys.
extern const FilterPolicy* NewBloomFilterPolicy(int bits_per_key);
}

#endif // !STORAGE_LEVELDB_INCLUDE_FILTER_POLICY_H_
