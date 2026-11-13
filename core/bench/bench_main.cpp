// core_bench: perf checks for the sim core. Exits non-zero when a budget is
// missed. (The M3 scenarios and baseline.json comparison arrive with M3.1.)
//
// This is the only place in core/ allowed to read a clock: it times the sim,
// it never feeds it.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "sim/rng.h"
#include "systems/shadows.h"

using namespace orbitfall;

namespace {

double nowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

// BRIEF §6 / M1.8: shadows for 1k casters in under 0.2 ms: build the intervals,
// merge them and test the 36 slots of the medium planet.
bool benchShadows() {
  const uint32_t casters = 1000, slots = 36, rounds = 2000;
  const float radius = 100.0f, budgetMs = 0.2f;
  Rng rng;
  rng.seed(42);
  std::vector<float> px(casters), py(casters), pr(casters);
  for (uint32_t i = 0; i < casters; ++i) {
    const float a = rng.range(-1.2f, 1.2f), d = rng.range(150.0f, 900.0f);
    px[i] = d * std::cos(a);
    py[i] = d * std::sin(a);
    pr[i] = rng.range(0.5f, 6.0f);  // small discs so the merge keeps many intervals
  }
  std::vector<ShadeInterval> scratch(casters);
  uint32_t shadedTotal = 0, merged = 0;
  double best = 1e9;
  for (int attempt = 0; attempt < 5; ++attempt) {
    const double t0 = nowMs();
    for (uint32_t round = 0; round < rounds; ++round) {
      const float sigma = 0.0005f * static_cast<float>(round);
      const float sx = std::cos(sigma), sy = std::sin(sigma);
      uint32_t n = 0;
      for (uint32_t i = 0; i < casters; ++i) {
        if (casterInterval(px[i], py[i], pr[i], sx, sy, radius, scratch[n])) ++n;
      }
      merged = mergeIntervals(scratch.data(), n);
      for (uint32_t s = 0; s < slots; ++s) {
        const float phi = 6.2831853f * static_cast<float>(s) / static_cast<float>(slots);
        if (std::cos(phi - sigma) > 0.0f && isShaded(scratch.data(), merged, radius * std::sin(phi - sigma))) {
          ++shadedTotal;
        }
      }
    }
    const double ms = (nowMs() - t0) / rounds;
    if (ms < best) best = ms;
  }
  const bool ok = best < budgetMs;
  std::printf("shadows-1k-casters: %.4f ms per tick (budget %.2f ms, %u merged intervals, checksum %u) %s\n",
              best, static_cast<double>(budgetMs), merged, shadedTotal, ok ? "OK" : "OVER BUDGET");
  return ok;
}

}  // namespace

int main() {
  bool ok = true;
  ok = benchShadows() && ok;
  return ok ? 0 : 1;
}
