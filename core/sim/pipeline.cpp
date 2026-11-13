#include "sim/pipeline.h"

#include "sim/world.h"
#include "systems/systems.h"

namespace orbitfall {

bool Pipeline::add(const char* name, SystemFn fn) {
  if (count >= kMaxSystems || fn == nullptr) return false;
  systems[count++] = SystemEntry{name, fn};
  return true;
}

void Pipeline::run(World& world) const {
  for (uint32_t i = 0; i < count; ++i) systems[i].fn(world, world.dt);
}

// Systems in tick order. Full order (BRIEF 5.3): applyCommands, planetRotation,
// sunlight, spawners, clusterLOD, enemyMovement, fallingObjects, spatialRebuild,
// towerTargeting, towerFire, projectileMovement, collisions, shields, damageResolve,
// landing, income, satelliteEffects, abilities, statusEffects, xp, cleanup, snapshot.
// Register each new system at its place in that order.
Pipeline defaultPipeline() {
  Pipeline p;
  p.add("applyCommands", applyCommands);
  p.add("planetRotation", planetRotation);
  p.add("sunlight", sunlight);
  p.add("enemyMovement", enemyMovement);
  p.add("spatialRebuild", spatialRebuild);
  p.add("towerTargeting", towerTargeting);
  p.add("towerFire", towerFire);
  p.add("projectileMovement", projectileMovement);
  p.add("collisions", collisions);
  p.add("landing", landing);
  p.add("income", income);
  p.add("cleanup", cleanup);
  p.add("advanceClock", advanceClock);
  return p;
}

}  // namespace orbitfall
