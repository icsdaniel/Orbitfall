#pragma once
// The tick pipeline: an ordered list of systems, each `void name(World&, float dt)`.
// The order is defined in pipeline.cpp (defaultPipeline).
#include <cstdint>

namespace orbitfall {

struct World;

using SystemFn = void (*)(World&, float dt);

struct SystemEntry {
  const char* name;
  SystemFn fn;
};

struct Pipeline {
  static constexpr uint32_t kMaxSystems = 48;
  SystemEntry systems[kMaxSystems] = {};
  uint32_t count = 0;

  // Appends a system; returns false when the pipeline is full.
  bool add(const char* name, SystemFn fn);
  // Runs one tick. Each system gets world.dt, which applyCommands sets from the time scale.
  void run(World& world) const;
};

// The game's systems in tick order.
Pipeline defaultPipeline();

// --- systems owned by core/sim ---
// Applies commands due this tick, eases the time scale and sets world.dt.
void applyCommands(World& world, float dt);
// Placeholder until real systems exist: advances world.simTime by dt.
void advanceClock(World& world, float dt);

}  // namespace orbitfall
