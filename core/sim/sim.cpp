#include "sim/sim.h"

namespace orbitfall {

Sim::Sim(const SimConfig& config) : pipeline_(defaultPipeline()) { initWorld(world_, config); }

void Sim::reset(const SimConfig& config) {
  initWorld(world_, config);
  accumulator_ = 0.0;
}

void Sim::step() {
  pipeline_.run(world_);
  ++world_.tick;
}

uint32_t Sim::advance(double realSeconds) {
  if (!(realSeconds > 0.0)) return 0;  // also rejects NaN
  const double fixed = static_cast<double>(kFixedDt);
  accumulator_ += realSeconds;
  uint32_t ran = 0;
  while (accumulator_ >= fixed && ran < maxTicksPerAdvance) {
    step();
    accumulator_ -= fixed;
    ++ran;
  }
  // Host fell too far behind: drop the backlog instead of spiralling.
  if (accumulator_ >= fixed) accumulator_ = 0.0;
  return ran;
}

}  // namespace orbitfall
