#pragma once
// Sim: owns a World and its Pipeline and drives the fixed 30 Hz step.
// The host supplies elapsed real time; core/ never reads a clock.
#include <cstdint>

#include "sim/pipeline.h"
#include "sim/world.h"

namespace orbitfall {

class Sim {
 public:
  explicit Sim(const SimConfig& config = SimConfig());

  // Restarts from tick 0 with a new config (same pipeline).
  void reset(const SimConfig& config);

  // Queues a command. Commands stamped earlier than the current tick apply on the next step.
  void push(const Command& command) { world_.commands.push(command); }

  // Runs exactly one tick.
  void step();

  // Adds real elapsed seconds and runs every whole tick that fits, up to
  // maxTicksPerAdvance (extra backlog is dropped). Returns the ticks run.
  uint32_t advance(double realSeconds);

  // Fraction of a tick left in the accumulator, in [0, 1): the render interpolation factor.
  float alpha() const { return static_cast<float>(accumulator_ / static_cast<double>(kFixedDt)); }

  uint64_t hash() const { return hashWorld(world_); }
  uint32_t tick() const { return world_.tick; }

  World& world() { return world_; }
  const World& world() const { return world_; }
  // Extra systems (tests, tools) can be appended before the first step.
  Pipeline& pipeline() { return pipeline_; }

  uint32_t maxTicksPerAdvance = 8;

 private:
  World world_;
  Pipeline pipeline_;
  double accumulator_ = 0.0;
};

}  // namespace orbitfall
