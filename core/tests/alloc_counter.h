#pragma once
// Counts every global operator new in the test binary (see alloc_counter.cpp).
// Read it before and after a region to prove the region does not allocate.
#include <cstdint>

namespace orbitfall_test {
uint64_t allocationCount();
}
