#include <iostream>
#include "testharness.h"


int main() {
	leveldb::test::RunAllTests();
	std::cout << "Hello World" << std::endl;

    return 0;
}
