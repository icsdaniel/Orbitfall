#include "sim/world.h"

#include "sim/hash.h"
#include "sim/mathx.h"
#include "systems/sun.h"

namespace orbitfall {
namespace {
constexpr uint32_t kGridCellsPerSide = 64;
}

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
  world.content = config.content;
  world.planet = nullptr;
  world.sun = nullptr;
  world.sunPath = nullptr;
  if (const ContentDB* db = config.content) {
    const bool hasSet = config.waveSet >= 0 && config.waveSet < static_cast<int>(db->waveSets.size());
    const int planet = hasSet ? db->waveSets[static_cast<size_t>(config.waveSet)].planet : config.planet;
    if (planet >= 0 && planet < static_cast<int>(db->planets.size())) {
      world.planet = &db->planets[static_cast<size_t>(planet)];
    }
    const int sun = hasSet ? db->waveSets[static_cast<size_t>(config.waveSet)].sun : config.sun;
    if (sun >= 0 && sun < static_cast<int>(db->suns.size())) {
      world.sun = &db->suns[static_cast<size_t>(sun)];
      world.sunPath = &sunPathFor(world.sun->type);
    }
  }
  world.sunBearing = world.sun ? wrapTau(world.sun->start) : 0.0f;
  world.lightMult = 1.0f;
  world.beamStrength = world.sun ? world.sun->baseStrength : 0.0f;
  world.incomePerSecond = 0.0f;
  world.planetRotation = 0.0f;
  world.planetAngVel = 0.0f;
  world.planetHp = world.planet ? world.planet->hp : 0.0f;
  world.planetHpMax = world.planetHp;
  world.phase = Phase::Wave;
  world.waveIndex = 0;
  world.enemiesSpawned = 0;
  world.stardust = world.planet ? world.planet->startStardust : 0.0f;
  world.slots.assign(world.planet ? static_cast<size_t>(world.planet->slots) : 0, BuildingSlot());
  world.slotTargets.assign(world.slots.size(), kInvalidIndex);
  world.rng.seed(config.seed);
  world.commandsApplied = 0;
  world.commandDigest = 0;
  // Re-reserve only when the capacity changes, so a restart does not allocate.
  if (world.enemies.capacity != config.enemyCapacity) world.enemies.reserve(config.enemyCapacity);
  world.enemies.clear();
  world.enemies.overflowCount = 0;
  if (world.projectiles.capacity != config.projectileCapacity) {
    world.projectiles.reserve(config.projectileCapacity);
  }
  world.projectiles.clear();
  world.projectiles.overflowCount = 0;
  // The grid covers the furthest spawn point; cells are a fixed fraction of it.
  world.maxEnemyRadius = 0.0f;
  float extent = world.planet ? world.planet->radius * 2.0f : 1.0f;
  if (const ContentDB* db = config.content) {
    if (db->rules.spawnRadius > extent) extent = db->rules.spawnRadius;
    for (const EnemyDef& def : db->enemies) {
      if (def.size > world.maxEnemyRadius) world.maxEnemyRadius = def.size;
      if (def.hasHover && def.hover.spawnRadius > extent) extent = def.hover.spawnRadius;
    }
  }
  const float halfExtent = extent * 1.05f;
  if (world.grid.items.size() != config.enemyCapacity || world.grid.cols != kGridCellsPerSide ||
      world.grid.minX != -halfExtent) {
    world.grid.configure(halfExtent, kGridCellsPerSide, config.enemyCapacity);
  }
  world.grid.count = 0;
  world.commands.clear();
  world.commands.reserve(config.commandCapacity);
  world.events.clear();
  world.events.totalEmitted = 0;
  world.events.reserve(config.eventCapacity);
}

void initWorld(World& world, const ContentDB& content, uint32_t waveSet, uint64_t seed) {
  SimConfig config;
  config.seed = static_cast<uint32_t>(seed ^ (seed >> 32));
  config.content = &content;
  config.waveSet = static_cast<int>(waveSet);
  initWorld(world, config);
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
  h.add(world.planetRotation);
  h.add(world.planetAngVel);
  h.add(world.sunBearing);
  h.add(world.lightMult);
  h.add(world.planetHp);
  h.add(world.phase);
  h.add(world.waveIndex);
  h.add(world.enemiesSpawned);
  h.add(world.stardust);
  for (const BuildingSlot& b : world.slots) {
    h.add(b.type);
    h.add(b.mode);
    h.add(b.flags);
    h.add(b.aimOffset);
    h.add(b.cooldown);
  }
  h.array(world.rng.s, 4);
  h.add(world.commandsApplied);
  h.add(world.commandDigest);
  h.add(world.events.totalEmitted);
  world.enemies.hashInto(h);
  world.projectiles.hashInto(h);
  return h.state;
}

}  // namespace orbitfall
