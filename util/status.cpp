#include <cassert>
#include <cstdint>
#include <cstring>
#include "status.h"

namespace leveldb {

const char* Status::CopyState(const char* state) {
	uint32_t size;
	memcpy(&size, state, sizeof(size)); // first get the length of array
	char* result = new char[size + 5];
	memcpy(result, state, size + 5);
	return result;
}

Status::Status(Code code, const Slice& msg, const Slice& msg2) {
	assert(code != kOk);
	const auto len1 = msg.size();
	const auto len2 = msg2.size();
	const auto size = len1 + (len2 ? (2 + len2) : 0);
	char* result = new char[size + 5];
	memcpy(result, &size, sizeof(size));
	result[4] = static_cast<char>(code);
	memcpy(result + 5, msg.data(), len1);
	if (len2) {
		result[5 + len1] = ':';
		result[6 + len1] = ' ';
		memcpy(result + 7 + len1, msg2.data(), len2);
	}
	state_ = result;
}

std::string Status::ToString() const {
	if (state_ == nullptr) {
		return "OK";
	}
	else {
		const char* type;
		switch (code()) {
		case kOk:
			type = "OK";
			break;
		case kNotFound:
			type = "NotFound: ";
			break;
		case kCorruption:
			type = "Corruption: ";
			break;
		case kNotSupported:
			type = "Not implemented: ";
			break;
		case kInvalidArgument:
			type = "Invalid argument: ";
			break;
		case kIOError:
			type = "IO error: ";
			break;
		default:
			char temp[30];
			snprintf(temp, sizeof(temp), "Unknown code(%d): ",
				static_cast<int>(code()));
			type = temp;
			break;
		}
		std::string result(type);
		uint32_t length;
		memcpy(&length, state_, sizeof(length));
		result.append(state_ + 5, length);
		return result;
	}
}

} // close namespace leveldb