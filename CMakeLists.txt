cmake_minimum_required(VERSION 3.0)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS)

project(leveldb)

add_executable(leveldb
	main.cpp
	util/arena.h
	util/arena.cpp
	util/random.h
	include/leveldb/slice.h
	include/leveldb/status.h
	util/status.cpp
	util/coding.h
	port/port.h
	port/port_stdcxx.h
	port/thread_annotations.h
	util/crc32c.h
	util/crc32c.cpp
	util/hash.h
	util/hash.cpp
	include/leveldb/filter_policy.h
	util/filter_policy.cpp
	util/bloom.cpp
	util/mutexlock.h
	util/logging.h
	util/logging.cpp
	include/leveldb/cache.h
	util/cache.cpp
	util/testutil.h
	util/testutil.cpp
)

target_include_directories(leveldb
  PRIVATE
    util
	include/leveldb
	port
)