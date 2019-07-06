#include <algorithm>
#include <stdint.h>
#include "comparator.h"
#include "slice.h"
#include "no_destructor.h"

namespace leveldb {

Comparator::~Comparator() { }

namespace {
class BytewiseComparatorImpl : public Comparator {
public:
	BytewiseComparatorImpl() = default;

	virtual const char* Name() const {
		return "leveldb.BytewiseComparator";
	}

	virtual int Compare(const Slice& a, const Slice& b) const {
		return a.compare(b);
	}

	virtual void FindShortestSeparator(
		std::string* start,
		const Slice& limit) const {
		// Find length of common prefix
		size_t min_length = std::min(start->size(), limit.size());
		size_t diff_index = 0;
		while ((diff_index < min_length) &&
			((*start)[diff_index] == limit[diff_index])) {
			diff_index++;
		}

		if (diff_index >= min_length) {
			// Do not shorten if one string is a prefix of the other
		}
		else {
			// 尝试执行字符start[diff_index]++，设置start长度为diff_index+1，并返回
			// ++条件：字符< oxff 并且字符+1 < limit上该index的字符
			uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
			if (diff_byte < static_cast<uint8_t>(0xff) &&
				diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
				(*start)[diff_index]++;
				start->resize(diff_index + 1);
				assert(Compare(*start, limit) < 0);
			}
		}
	}

	virtual void FindShortSuccessor(std::string* key) const {
		// Find first character that can be incremented
		size_t n = key->size();
		for (size_t i = 0; i < n; i++) {
			const uint8_t byte = (*key)[i];
			if (byte != static_cast<uint8_t>(0xff)) {
				(*key)[i] = byte + 1;
				key->resize(i + 1);
				return;
			}
		}
		// *key is a run of 0xffs.  Leave it alone.
	}
};
}  // namespace

const Comparator* BytewiseComparator() {
	static NoDestructor<BytewiseComparatorImpl> singleton;
	return singleton.get();
}

}  // namespace leveldb