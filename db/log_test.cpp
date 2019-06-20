#include "log_reader.h"
#include "log_writer.h"
#include "env.h"
#include "coding.h"
#include "crc32c.h"
#include "random.h"
#include "testharness.h"

namespace leveldb {
namespace log {

// Construct a string of the specified length made out of the supplied
// partial string.
static std::string BigString(const std::string& partial_string, size_t n) {
	std::string result;
	while (result.size() < n) {
		result.append(partial_string);
	}
	result.resize(n);
	return result;
}

// Construct a string from a number
static std::string NumberString(int n) {
	char buf[50];
	snprintf(buf, sizeof(buf), "%d.", n);
	return std::string(buf);
}

// Return a skewed potentially long string
static std::string RandomSkewedString(int i, Random* rnd) {
	return BigString(NumberString(i), rnd->Skewed(17));
}

class LogTest {
private:
	// StringDest就是一个WritableFile，向这里写入数据
	class StringDest : public WritableFile {
	public:
		std::string contents_;

		virtual Status Close() { return Status::OK(); }
		virtual Status Flush() { return Status::OK(); }
		virtual Status Sync() { return Status::OK(); }
		virtual Status Append(const Slice& slice) {
			contents_.append(slice.data(), slice.size());
			return Status::OK();
		}
	};

	// StringSource就是一个SequentialFile，从这里读取数据
	class StringSource : public SequentialFile {
	public:
		Slice contents_;
		bool force_error_;
		bool returned_partial_;
		StringSource() : force_error_(false), returned_partial_(false) { }

		virtual Status Read(size_t n, Slice* result, char* scratch) {
			// 在出现了error之后不能再进行Read操作
			ASSERT_TRUE(!returned_partial_) << "must not Read() after eof/error";

			if (force_error_) {
				force_error_ = false;
				returned_partial_ = true;
				return Status::Corruption("read error");
			}

			if (contents_.size() < n) {
				n = contents_.size();
				returned_partial_ = true;
			}
			*result = Slice(contents_.data(), n);
			contents_.remove_prefix(n);
			return Status::OK();
		}

		virtual Status Skip(uint64_t n) {
			if (n > contents_.size()) {
				contents_.clear();
				return Status::NotFound("in-memory file skipped past end");
			}

			contents_.remove_prefix(n);

			return Status::OK();
		}
	};

	class ReportCollector : public Reader::Reporter {
	public:
		size_t dropped_bytes_;
		std::string message_;

		ReportCollector() : dropped_bytes_(0) { }
		virtual void Corruption(size_t bytes, const Status& status) {
			dropped_bytes_ += bytes;
			message_.append(status.ToString());
		}
	};

	StringDest dest_;
	StringSource source_;
	ReportCollector report_;
	bool reading_;
	Writer* writer_;
	Reader* reader_;

	// Record metadata for testing initial offset functionality
	static size_t initial_offset_record_sizes_[];
	static uint64_t initial_offset_last_record_offsets_[];
	static int num_initial_offset_records_;

public:
	LogTest() : reading_(false),
		writer_(new Writer(&dest_)),
		reader_(new Reader(&source_, &report_, true/*checksum*/,
			0/*initial_offset*/)) {
	}

	~LogTest() {
		delete writer_;
		delete reader_;
	}

	void ReopenForAppend() {
		// 删除writer并且重新建立一个writer
		// 在这种情况下，之前写下的数据应该还在
		delete writer_;
		writer_ = new Writer(&dest_, dest_.contents_.size());
	}

	void Write(const std::string& msg) {
		// 首先确认当前处于read状态下
		// 之后调用writer添加record
		ASSERT_TRUE(!reading_) << "Write() after starting to read";
		writer_->AddRecord(Slice(msg));
	}

	size_t WrittenBytes() const {
		return dest_.contents_.size();
	}

	std::string Read() {
		// 如果不是read状态就设置成read状态
		// 而且把WritableFile里面的内容复制到SequentialFile里面
		if (!reading_) {
			reading_ = true;
			source_.contents_ = Slice(dest_.contents_);
		}
		std::string scratch;
		Slice record;
		// 调用reader的ReadRecord方法读取数据
		if (reader_->ReadRecord(&record, &scratch)) {
			return record.ToString();
		}
		else {
			return "EOF";
		}
	}

	void IncrementByte(int offset, int delta) {
		dest_.contents_[offset] += delta;
	}

	void SetByte(int offset, char new_byte) {
		dest_.contents_[offset] = new_byte;
	}

	void ShrinkSize(int bytes) {
		dest_.contents_.resize(dest_.contents_.size() - bytes);
	}

	void FixChecksum(int header_offset, int len) {
		// Compute crc of type/len/data
		// contents_的前6个bytes是checksum和length，从第7个byte开始计算crc32
		uint32_t crc = crc32c::Value(&dest_.contents_[header_offset + 6], 1 + len);
		crc = crc32c::Mask(crc);
		EncodeFixed32(&dest_.contents_[header_offset], crc);
	}

	void ForceError() {
		source_.force_error_ = true;
	}

	size_t DroppedBytes() const {
		return report_.dropped_bytes_;
	}

	std::string ReportMessage() const {
		return report_.message_;
	}

	// Returns OK iff recorded error message contains "msg"
	std::string MatchError(const std::string& msg) const {
		if (report_.message_.find(msg) == std::string::npos) {
			return report_.message_;
		}
		else {
			return "OK";
		}
	}

	void WriteInitialOffsetLog() {
		for (int i = 0; i < num_initial_offset_records_; i++) {
			std::string record(initial_offset_record_sizes_[i],
				static_cast<char>('a' + i));
			Write(record);
		}
	}

	void StartReadingAt(uint64_t initial_offset) {
		delete reader_;
		reader_ = new Reader(&source_, &report_, true/*checksum*/, initial_offset);
	}

	void CheckOffsetPastEndReturnsNoRecords(uint64_t offset_past_end) {
		WriteInitialOffsetLog();
		reading_ = true;
		source_.contents_ = Slice(dest_.contents_);
		Reader* offset_reader = new Reader(&source_, &report_, true/*checksum*/,
			WrittenBytes() + offset_past_end);
		Slice record;
		std::string scratch;
		ASSERT_TRUE(!offset_reader->ReadRecord(&record, &scratch));
		delete offset_reader;
	}

	void CheckInitialOffsetRecord(uint64_t initial_offset,
		int expected_record_offset) {
		WriteInitialOffsetLog();
		reading_ = true;
		source_.contents_ = Slice(dest_.contents_);
		Reader* offset_reader = new Reader(&source_, &report_, true/*checksum*/,
			initial_offset);

		// Read all records from expected_record_offset through the last one.
		ASSERT_LT(expected_record_offset, num_initial_offset_records_);
		for (; expected_record_offset < num_initial_offset_records_;
			++expected_record_offset) {
			Slice record;
			std::string scratch;
			ASSERT_TRUE(offset_reader->ReadRecord(&record, &scratch));
			ASSERT_EQ(initial_offset_record_sizes_[expected_record_offset],
				record.size());
			ASSERT_EQ(initial_offset_last_record_offsets_[expected_record_offset],
				offset_reader->LastRecordOffset());
			ASSERT_EQ((char)('a' + expected_record_offset), record.data()[0]);
		}
		delete offset_reader;
	}
};

size_t LogTest::initial_offset_record_sizes_[] = {
	10000,  // Two sizable records in first block
	10000,
	2 * log::kBlockSize - 1000,  // Span three blocks
	1,
	13716,  // Consume all but two bytes of block 3.
	log::kBlockSize - kHeaderSize, // Consume the entirely of block 4.
};

uint64_t LogTest::initial_offset_last_record_offsets_[] = {
	0,
	kHeaderSize + 10000,
	2 * (kHeaderSize + 10000),
	2 * (kHeaderSize + 10000) +
		(2 * log::kBlockSize - 1000) + 3 * kHeaderSize,
	2 * (kHeaderSize + 10000) +
		(2 * log::kBlockSize - 1000) + 3 * kHeaderSize
		+ kHeaderSize + 1,
	3 * log::kBlockSize,
};

// LogTest::initial_offset_last_record_offsets_ must be defined before this.
int LogTest::num_initial_offset_records_ = sizeof(LogTest::initial_offset_last_record_offsets_) / sizeof(uint64_t);

TEST(LogTest, Empty) {
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, Fragmentation) {
	Write("small");
	Write(BigString("medium", 50000));
	Write(BigString("large", 100000));
	ASSERT_EQ("small", Read());
	ASSERT_EQ(BigString("medium", 50000), Read());
	ASSERT_EQ(BigString("large", 100000), Read());
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, MarginalTrailer) {
	// Make a trailer that is exactly the same length as an empty record
	const int n = kBlockSize - 2 * kHeaderSize;
	Write(BigString("foo", n));
	ASSERT_EQ(kBlockSize - kHeaderSize, WrittenBytes());
	Write("");
	Write("bar");
	ASSERT_EQ(BigString("foo", n), Read());
	ASSERT_EQ("", Read());
	ASSERT_EQ("bar", Read());
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, MarginalTrailer2) {
	// Make a trailer that is exactly the same length as an empty record.
	const int n = kBlockSize - 2 * kHeaderSize;
	Write(BigString("foo", n));
	ASSERT_EQ(kBlockSize - kHeaderSize, WrittenBytes());
	Write("bar");
	ASSERT_EQ(BigString("foo", n), Read());
	ASSERT_EQ("bar", Read());
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(0, DroppedBytes());
	ASSERT_EQ("", ReportMessage());
}

TEST(LogTest, ShortTrailer) {
	const int n = kBlockSize - 2 * kHeaderSize + 4;
	Write(BigString("foo", n));
	ASSERT_EQ(kBlockSize - kHeaderSize + 4, WrittenBytes());
	Write("");
	Write("bar");
	ASSERT_EQ(BigString("foo", n), Read());
	ASSERT_EQ("", Read());
	ASSERT_EQ("bar", Read());
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, AlignedEof) {
	const int n = kBlockSize - 2 * kHeaderSize + 4;
	Write(BigString("foo", n));
	ASSERT_EQ(kBlockSize - kHeaderSize + 4, WrittenBytes());
	ASSERT_EQ(BigString("foo", n), Read());
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, OpenForAppend) {
	Write("hello");
	ReopenForAppend();
	Write("world");
	ASSERT_EQ("hello", Read());
	ASSERT_EQ("world", Read());
	ASSERT_EQ("EOF", Read());
}

TEST(LogTest, RandomRead) {
	const int N = 500;
	Random write_rnd(301);
	for (int i = 0; i < N; i++) {
		Write(RandomSkewedString(i, &write_rnd));
	}
	Random read_rnd(301);
	for (int i = 0; i < N; i++) {
		ASSERT_EQ(RandomSkewedString(i, &read_rnd), Read());
	}
	ASSERT_EQ("EOF", Read());
}

// Tests of all the error paths in log_reader.cpp follow:

TEST(LogTest, ReadError) {
	Write("foo");
	// 设置SequentialFile有error
	ForceError();
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(kBlockSize, DroppedBytes());
	ASSERT_EQ("OK", MatchError("read error"));
}

TEST(LogTest, BadRecordType) {
	Write("foo");
	// Type is stored in header[6]
	IncrementByte(6, 100);
	// 因为之前写入的foo是3个bytes，所以这里FixCheckSum的第二个参数是3
	FixChecksum(0, 3);
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(3, DroppedBytes());
	ASSERT_EQ("OK", MatchError("unknown record type"));
}

TEST(LogTest, TruncatedTrailingRecordIsIgnored) {
	Write("foo");
	ShrinkSize(4);  // Drop all payload as well as a header type
	ASSERT_EQ("EOF", Read());
	// Truncated last record is ignored, not treated as an error.
	ASSERT_EQ(0, DroppedBytes());
	ASSERT_EQ("", ReportMessage());
}

TEST(LogTest, BadLength) {
	const int kPayloadSize = kBlockSize - kHeaderSize;
	Write(BigString("bar", kPayloadSize));
	Write("foo");
	// Least significant size byte is stored in header[4]
	IncrementByte(4, 1);
	// 因为上一行代码的修改，导致含有bar的那个record length偏大
	// 而在这种情况下，reader会drop之前的块，抛出错误但继续阅读
	// 所以会读到foo这条记录
	ASSERT_EQ("foo", Read());
	ASSERT_EQ(kBlockSize, DroppedBytes());
	ASSERT_EQ("OK", MatchError("bad record length"));
}

TEST(LogTest, BadLengthAtEndIsIgnored) {
	Write("foo");
	ShrinkSize(1);
	// 和上一个测试同样的错误，实际的长度(也就是从读入buffer里面的)
	// 小于record中记录的length
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(0, DroppedBytes());
	ASSERT_EQ("", ReportMessage());
}

TEST(LogTest, ChecksumMismatch) {
	Write("foo");
	// Checksum存储在前4个bytes，这个操作更改了checksum的值
	IncrementByte(0, 10);
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(10, DroppedBytes());
	ASSERT_EQ("OK", MatchError("checksum mismatch"));
}

TEST(LogTest, UnexpectedMiddleType) {
	Write("foo");
	SetByte(6, kMiddleType);
	FixChecksum(0, 3);
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(3, DroppedBytes());
	ASSERT_EQ("OK", MatchError("missing start"));
}

TEST(LogTest, UnexpectedLastType) {
	Write("foo");
	SetByte(6, kLastType);
	FixChecksum(0, 3);
	ASSERT_EQ("EOF", Read());
	ASSERT_EQ(3, DroppedBytes());
	ASSERT_EQ("OK", MatchError("missing start"));
}

}  // namespace log
}  // namespace leveldb