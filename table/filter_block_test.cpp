﻿#include "filter_block.h"

#include "filter_policy.h"
#include "coding.h"
#include "hash.h"
#include "logging.h"
#include "testharness.h"

namespace leveldb {

// For testing: emit an array with one hash value per key
class TestHashFilter : public FilterPolicy {
public:
	virtual const char* Name() const {
		return "TestHashFilter";
	}

	virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const {
		for (int i = 0; i < n; i++) {
			uint32_t h = Hash(keys[i].data(), keys[i].size(), 1);
			PutFixed32(dst, h);
		}
	}

	virtual bool KeyMayMatch(const Slice& key, const Slice& filter) const {
		uint32_t h = Hash(key.data(), key.size(), 1);
		for (size_t i = 0; i + 4 <= filter.size(); i += 4) {
			if (h == DecodeFixed32(filter.data() + i)) {
				return true;
			}
		}
		return false;
	}
};

class FilterBlockTest {
public:
	TestHashFilter policy_;
};

TEST(FilterBlockTest, EmptyBuilder) {
	FilterBlockBuilder builder(&policy_);
	Slice block = builder.Finish();
	ASSERT_EQ("\\x00\\x00\\x00\\x00\\x0b", EscapeString(block));
	FilterBlockReader reader(&policy_, block);
	// 因为此时FilterBlockBuilder的num_为0，所以怎么找都是true
	ASSERT_TRUE(reader.KeyMayMatch(0, "foo"));
	ASSERT_TRUE(reader.KeyMayMatch(100000, "foo"));
}

TEST(FilterBlockTest, SingleChunk) {
	FilterBlockBuilder builder(&policy_);
	builder.StartBlock(100);
	builder.AddKey("foo");
	builder.AddKey("bar");
	builder.AddKey("box");
	builder.StartBlock(200);
	builder.AddKey("box");
	builder.StartBlock(300);
	builder.AddKey("hello");
	Slice block = builder.Finish();
	FilterBlockReader reader(&policy_, block);
	ASSERT_TRUE(reader.KeyMayMatch(100, "foo"));
	ASSERT_TRUE(reader.KeyMayMatch(100, "bar"));
	ASSERT_TRUE(reader.KeyMayMatch(100, "box"));
	ASSERT_TRUE(reader.KeyMayMatch(100, "hello"));
	ASSERT_TRUE(reader.KeyMayMatch(100, "foo"));
	ASSERT_TRUE(!reader.KeyMayMatch(100, "missing"));
	ASSERT_TRUE(!reader.KeyMayMatch(100, "other"));
}

TEST(FilterBlockTest, MultiChunk) {
	FilterBlockBuilder builder(&policy_);

	// First filter
	builder.StartBlock(0);
	builder.AddKey("foo");
	builder.StartBlock(2000);
	builder.AddKey("bar");

	// Second filter
	builder.StartBlock(3100);
	builder.AddKey("box");

	// Third filter is empty

	// Last filter
	builder.StartBlock(9000);
	builder.AddKey("box");
	builder.AddKey("hello");

	Slice block = builder.Finish();
	FilterBlockReader reader(&policy_, block);

	// Check first filter
	ASSERT_TRUE(reader.KeyMayMatch(0, "foo"));
	ASSERT_TRUE(reader.KeyMayMatch(2000, "bar"));
	ASSERT_TRUE(!reader.KeyMayMatch(0, "box"));
	ASSERT_TRUE(!reader.KeyMayMatch(0, "hello"));

	// Check second filter
	ASSERT_TRUE(reader.KeyMayMatch(3100, "box"));
	ASSERT_TRUE(!reader.KeyMayMatch(3100, "foo"));
	ASSERT_TRUE(!reader.KeyMayMatch(3100, "bar"));
	ASSERT_TRUE(!reader.KeyMayMatch(3100, "hello"));

	// Check third filter (empty)
	ASSERT_TRUE(!reader.KeyMayMatch(4100, "foo"));
	ASSERT_TRUE(!reader.KeyMayMatch(4100, "bar"));
	ASSERT_TRUE(!reader.KeyMayMatch(4100, "box"));
	ASSERT_TRUE(!reader.KeyMayMatch(4100, "hello"));

	// Check last filter
	ASSERT_TRUE(reader.KeyMayMatch(9000, "box"));
	ASSERT_TRUE(reader.KeyMayMatch(9000, "hello"));
	ASSERT_TRUE(!reader.KeyMayMatch(9000, "foo"));
	ASSERT_TRUE(!reader.KeyMayMatch(9000, "bar"));
}

}  // namespace leveldb