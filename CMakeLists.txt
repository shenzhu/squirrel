cmake_minimum_required(VERSION 3.0)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

project(leveldb)

add_executable(leveldb
	main.cpp
	util/arena.h
	util/arena.cpp
	util/arena_test.cpp
	util/random.h
	include/leveldb/slice.h
	include/leveldb/status.h
	util/status.cpp
	util/coding.h
	util/coding.cpp
	port/port.h
	port/port_stdcxx.h
	port/thread_annotations.h
	util/crc32c.h
	util/crc32c.cpp
	util/crc32c_test.cpp
	util/hash.h
	util/hash.cpp
	util/hash_test.cpp
	include/leveldb/filter_policy.h
	util/filter_policy.cpp
	util/bloom.cpp
	util/bloom_test.cpp
	util/mutexlock.h
	util/logging.h
	util/logging.cpp
	include/leveldb/cache.h
	util/cache.cpp
	util/testutil.h
	util/testutil.cpp
	util/testharness.h
	util/testharness.cpp
	include/leveldb/env.h
	util/env.cpp
	util/windows_logger.h
	util/env_windows_tesT_helper.h
	util/env_windows.cpp
	util/env_windows_test.cpp
	util/no_destructor.h
	util/no_destructor.cpp
	include/leveldb/comparator.h
	util/comparator.cpp
	include/leveldb/options.h
	util/options.cpp
	db/log_format.h
)

target_include_directories(leveldb
  PRIVATE
    util
	include/leveldb
	port
	db
)