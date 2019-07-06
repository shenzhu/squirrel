#include "dbformat.h"
#include "logging.h"
#include "testharness.h"

namespace leveldb {

static std::string IKey(const std::string& user_key,
	uint64_t seq,
	ValueType vt) {
	std::string encoded;
	AppendInternalKey(&encoded, ParsedInternalKey(user_key, seq, vt));
	return encoded;
}

static std::string Shorten(const std::string& s, const std::string& l) {
	std::string result = s;
	InternalKeyComparator(BytewiseComparator()).FindShortestSeparator(&result, l);
	return result;
}

static std::string ShortSuccessor(const std::string& s) {
	std::string result = s;
	InternalKeyComparator(BytewiseComparator()).FindShortSuccessor(&result);
	return result;
}

static void TestKey(const std::string& key,
	uint64_t seq,
	ValueType vt) {
	std::string encoded = IKey(key, seq, vt);

	Slice in(encoded);
	ParsedInternalKey decoded("", 0, kTypeValue);

	ASSERT_TRUE(ParseInternalKey(in, &decoded));
	ASSERT_EQ(key, decoded.user_key.ToString());
	ASSERT_EQ(seq, decoded.sequence);
	ASSERT_EQ(vt, decoded.type);

	ASSERT_TRUE(!ParseInternalKey(Slice("bar"), &decoded));
}

class FormatTest { };

TEST(FormatTest, InternalKey_EncodeDecode) {
	const char* keys[] = { "", "k", "hello", "longggggggggggggggggggggg" };
	const uint64_t seq[] = {
		1, 2, 3,
		(1ull << 8) - 1, 1ull << 8, (1ul << 8) + 1,
		(1ull << 16) - 1, 1ull << 16, (1ull << 16) + 1,
		(1ull << 32) - 1, 1ull << 32, (1ull << 32) + 1
	};
	for (int k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
		for (int s = 0; s < sizeof(seq) / sizeof(seq[0]); s++) {
			TestKey(keys[k], seq[s], kTypeValue);
			TestKey("hello", 1, kTypeDeletion);
		}
	}
}

TEST(FormatTest, InternalKeyShortSeparator) {
	// When user keys are same
	// 注意FindShortestSeparator只是针对user_key来说的
	// 对于剩下的两个参数没有影响，所以这一组test中，即使其他两个参数不一样
	// 但是因为user_key都一样，所以都一样
	ASSERT_EQ(IKey("foo", 100, kTypeValue),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("foo", 99, kTypeValue)));
	ASSERT_EQ(IKey("foo", 100, kTypeValue),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("foo", 101, kTypeValue)));
	ASSERT_EQ(IKey("foo", 100, kTypeValue),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("foo", 100, kTypeValue)));
	ASSERT_EQ(IKey("foo", 100, kTypeValue),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("foo", 100, kTypeDeletion)));

	// When user keys are misordered
	// "foo"已经大于"bar"，所以user_key不会改变
	ASSERT_EQ(IKey("foo", 100, kTypeValue),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("bar", 99, kTypeValue)));

	// When user keys are different, but correctly ordered
	ASSERT_EQ(IKey("g", kMaxSequenceNumber, kValueTypeForSeek),
		Shorten(IKey("foo", 100, kTypeValue),
			IKey("hello", 200, kTypeValue)));

	// When limit user key is prefix of start user key
	ASSERT_EQ(IKey("foobar", 100, kTypeValue),
		Shorten(IKey("foobar", 100, kTypeValue),
			IKey("foo", 200, kTypeValue)));
}

TEST(FormatTest, InternalKeyShortestSuccessor) {
	ASSERT_EQ(IKey("g", kMaxSequenceNumber, kValueTypeForSeek),
		ShortSuccessor(IKey("foo", 100, kTypeValue)));
	ASSERT_EQ(IKey("\xff\xff", 100, kTypeValue),
		ShortSuccessor(IKey("\xff\xff", 100, kTypeValue)));
}

}  // namespace leveldb