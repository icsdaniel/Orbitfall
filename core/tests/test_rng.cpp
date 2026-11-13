#include "doctest/doctest.h"
#include "sim/rng.h"

using orbitfall::Rng;

TEST_CASE("rng: xoshiro128** reference outputs for state {1,2,3,4}") {
  Rng r;  // default state is {1,2,3,4}
  CHECK(r.nextU32() == 11520u);
  CHECK(r.nextU32() == 0u);
  CHECK(r.nextU32() == 5927040u);
}

TEST_CASE("rng: same seed gives the same sequence, another seed differs") {
  Rng a, b, c;
  a.seed(1234);
  b.seed(1234);
  c.seed(1235);
  bool anyDifferent = false;
  for (int i = 0; i < 1000; ++i) {
    const uint32_t va = a.nextU32();
    CHECK(va == b.nextU32());
    if (va != c.nextU32()) anyDifferent = true;
  }
  CHECK(anyDifferent);
}

TEST_CASE("rng: seed 0 is valid and floats stay in range") {
  Rng r;
  r.seed(0);
  CHECK((r.s[0] | r.s[1] | r.s[2] | r.s[3]) != 0u);
  double sum = 0.0;
  const int n = 20000;
  for (int i = 0; i < n; ++i) {
    const float f = r.nextFloat();
    REQUIRE(f >= 0.0f);
    REQUIRE(f < 1.0f);
    sum += f;
  }
  CHECK(sum / n == doctest::Approx(0.5).epsilon(0.02));
  for (int i = 0; i < 1000; ++i) {
    const float v = r.range(-3.0f, 5.0f);
    REQUIRE(v >= -3.0f);
    REQUIRE(v < 5.0f);
    REQUIRE(r.nextBelow(7) < 7u);
  }
  CHECK(r.nextBelow(0) == 0u);
}
