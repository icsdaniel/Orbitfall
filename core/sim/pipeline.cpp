#include "sim/pipeline.h"

#include "sim/world.h"
#include "systems/falling.h"
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
  p.add("spawners", spawners);
  p.add("enemyMovement", enemyMovement);
  p.add("spatialRebuild", spatialRebuild);
  p.add("towerTargeting", towerTargeting);
  p.add("towerFire", towerFire);
  p.add("beamFire", beamFire);  // M2.6: Sun Lens beams (continuous, no projectile)
  p.add("projectileMovement", projectileMovement);
  p.add("collisions", collisions);
  // M2.10: falling objects move and are telegraphed, shields recharge and
  // absorb, whatever is left hits the planet (HP only).
  p.add("fallingObjects", fallingObjects);
  p.add("shields", shields);
  p.add("fallingImpact", fallingImpact);
  p.add("landing", landing);
  // M2.5: Spores regrow a layer when left alone; Splitters killed this tick
  // split before cleanup removes them.
  p.add("regrow", regrow);
  p.add("splits", splits);
  p.add("drills", drills);  // M2.7: before income, so overheated drills do not earn
  p.add("income", income);
  p.add("abilities", abilities);  // M2.6: active ability timers (light-charged)
  p.add("cleanup", cleanup);
  p.add("waveFlow", waveFlow);
  p.add("advanceClock", advanceClock);
  return p;
}

}  // namespace orbitfall
