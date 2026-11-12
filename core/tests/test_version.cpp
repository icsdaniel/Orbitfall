#include "doctest/doctest.h"
#include "sim/version.h"
#include <cstring>
TEST_CASE("core builds and reports its version") {
  CHECK(orbitfall::core_answer() == 42);
  CHECK(std::strncmp(orbitfall::core_version(), "orbitfall-core", 14) == 0);
}
