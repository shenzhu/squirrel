cmake_minimum_required(VERSION 3.9)

project(leveldb VERSION 1.0.0)

# This project requires C++11
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 11)

# Add configure file, some pre-defined variables
configure_file(
    ${PROJECT_SOURCE_DIR}/port/port_config.h.in
	${PROJECT_SOURCE_DIR}/port/port_config.h
)

# Load cmake module: TestBigEndian
include(TestBigEndian)
test_big_endian(LEVELDB_IS_BIG_ENDIAN)

include(CheckLibraryExists)
check_library_exists(snappy snappy_compress "" HAVE_SNAPPY)

if (WIN32)
	set(LEVELDB_PLATFORM_NAME LEVELDB_PLATFORM_WINDOWS)
else (WIN32)
	set(LEVELDB_PLATFORM_NAME LEVELDB_PLATFORM_POSIX)
endif (WIN32)

# PROJECT_SOURCE_DIR: the folder of the CMakeLists.txt containing the most recent project command
# CMAKE_SOURCE_DIR: top level folder where CMakeLists.txt
# https://stackoverflow.com/questions/32028667/are-cmake-source-dir-and-project-source-dir-the-same-in-cmake
# PROJECT_BINARY_DIR: where compile happens
add_executable(leveldb
	${PROJECT_SOURCE_DIR}/port/port_config.h
	${PROJECT_SOURCE_DIR}/main.cpp
	${PROJECT_SOURCE_DIR}/util/arena.h
	${PROJECT_SOURCE_DIR}/util/arena.cpp
	${PROJECT_SOURCE_DIR}/util/arena_test.cpp
	${PROJECT_SOURCE_DIR}/util/random.h
	${PROJECT_SOURCE_DIR}/include/leveldb/slice.h
	${PROJECT_SOURCE_DIR}/include/leveldb/status.h
	${PROJECT_SOURCE_DIR}/util/status.cpp
	${PROJECT_SOURCE_DIR}/util/coding.h
	${PROJECT_SOURCE_DIR}/util/coding.cpp
	${PROJECT_SOURCE_DIR}/port/port.h
	${PROJECT_SOURCE_DIR}/port/port_stdcxx.h
	${PROJECT_SOURCE_DIR}/port/thread_annotations.h
	${PROJECT_SOURCE_DIR}/util/crc32c.h
	${PROJECT_SOURCE_DIR}/util/crc32c.cpp
	${PROJECT_SOURCE_DIR}/util/crc32c_test.cpp
	${PROJECT_SOURCE_DIR}/util/hash.h
	${PROJECT_SOURCE_DIR}/util/hash.cpp
	${PROJECT_SOURCE_DIR}/util/hash_test.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/filter_policy.h
	${PROJECT_SOURCE_DIR}/util/filter_policy.cpp
	${PROJECT_SOURCE_DIR}/util/bloom.cpp
	${PROJECT_SOURCE_DIR}/util/bloom_test.cpp
	${PROJECT_SOURCE_DIR}/util/mutexlock.h
	${PROJECT_SOURCE_DIR}/util/logging.h
	${PROJECT_SOURCE_DIR}/util/logging.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/cache.h
	${PROJECT_SOURCE_DIR}/util/cache.cpp
	${PROJECT_SOURCE_DIR}/util/testutil.h
	${PROJECT_SOURCE_DIR}/util/testutil.cpp
	${PROJECT_SOURCE_DIR}/util/testharness.h
	${PROJECT_SOURCE_DIR}/util/testharness.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/env.h
	${PROJECT_SOURCE_DIR}/util/env.cpp
	${PROJECT_SOURCE_DIR}/util/no_destructor.h
	${PROJECT_SOURCE_DIR}/util/no_destructor.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/comparator.h
	${PROJECT_SOURCE_DIR}/util/comparator.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/options.h
	${PROJECT_SOURCE_DIR}/util/options.cpp
	${PROJECT_SOURCE_DIR}/db/log_format.h
	${PROJECT_SOURCE_DIR}/db/log_writer.h
	${PROJECT_SOURCE_DIR}/db/log_writer.cpp
	${PROJECT_SOURCE_DIR}/db/log_reader.h
	${PROJECT_SOURCE_DIR}/db/log_reader.cpp
	${PROJECT_SOURCE_DIR}/db/log_test.cpp
	${PROJECT_SOURCE_DIR}/table/block_builder.h
	${PROJECT_SOURCE_DIR}/table/block_builder.cpp
	${PROJECT_SOURCE_DIR}/table/filter_block.h
	${PROJECT_SOURCE_DIR}/table/filter_block.cpp
	${PROJECT_SOURCE_DIR}/table/filter_block_test.cpp
	${PROJECT_SOURCE_DIR}/include/leveldb/iterator.h
	${PROJECT_SOURCE_DIR}/table/iterator.cpp
	${PROJECT_SOURCE_DIR}/table/iterator_wrapper.h
	${PROJECT_SOURCE_DIR}/table/format.h
	${PROJECT_SOURCE_DIR}/table/format.cpp
	${PROJECT_SOURCE_DIR}/table/block.h
	${PROJECT_SOURCE_DIR}/table/block.cpp
)

# Detect platform
# See https://stackoverflow.com/questions/9160335/os-specific-instructions-in-cmake-how-to
if (WIN32)
	target_sources(leveldb
	  PRIVATE
		${PROJECT_SOURCE_DIR}/util/windows_logger.h
		${PROJECT_SOURCE_DIR}/util/env_windows_test_helper.h
		${PROJECT_SOURCE_DIR}/util/env_windows.cpp
		${PROJECT_SOURCE_DIR}/util/env_windows_test.cpp
	)
else (WIN32)
	target_sources(leveldb
	  PRIVATE
	    ${PROJECT_SOURCE_DIR}/util/posix_logger.h
		${PROJECT_SOURCE_DIR}/util/env_posix_test_helper.h
		${PROJECT_SOURCE_DIR}/util/env_posix.cpp
		${PROJECT_SOURCE_DIR}/util/env_posix_test.cpp
	)
endif (WIN32)

target_include_directories(leveldb
  PRIVATE
    util
	include/leveldb
	port
	db
	table
)

# This should be put after add_executable
target_compile_definitions(leveldb
  PRIVATE
    LEVELDB_HAS_PORT_CONFIG_H=1
	${LEVELDB_PLATFORM_NAME}=1
)

if(HAVE_SNAPPY)
	target_link_libraries(leveldb snappy)
endif(HAVE_SNAPPY)