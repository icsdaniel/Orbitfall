#include "sim/world.h"

#include "sim/hash.h"

namespace orbitfall {

void World::emit(EventType type, int32_t a, int32_t b, float x, float y, float value) {
  Event e;
  e.tick = tick;
  e.type = type;
  e.a = a;
  e.b = b;
  e.x = x;
  e.y = y;
  e.value = value;
  events.push(e);
}

void initWorld(World& world, const SimConfig& config) {
  world.seed = config.seed;
  world.tick = 0;
  world.simTime = 0.0;
  world.dt = 0.0f;
  world.timeScale = 1.0f;
  world.timeScaleFrom = 1.0f;
  world.timeScaleTarget = 1.0f;
  world.easeTicksLeft = 0;
  world.easeTicksTotal = 0;
  world.rotateInput = 0.0f;
  world.rng.seed(config.seed);
  world.commandsApplied = 0;
  world.commandDigest = 0;
  world.commands.clear();
  world.commands.reserve(config.commandCapacity);
  world.events.clear();
  world.events.totalEmitted = 0;
  world.events.reserve(config.eventCapacity);
}

uint64_t hashWorld(const World& world) {
  Hasher h;
  h.add(world.seed);
  h.add(world.tick);
  h.add(world.simTime);
  h.add(world.dt);
  h.add(world.timeScale);
  h.add(world.timeScaleFrom);
  h.add(world.timeScaleTarget);
  h.add(world.easeTicksLeft);
  h.add(world.easeTicksTotal);
  h.add(world.rotateInput);
  h.array(world.rng.s, 4);
  h.add(world.commandsApplied);
  h.add(world.commandDigest);
  h.add(world.events.totalEmitted);
  return h.state;
}

}  // namespace orbitfall
