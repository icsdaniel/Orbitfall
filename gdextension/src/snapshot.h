#pragma once
// Snapshot: the plain-data copy of the sim state that SimServer's getters read.
//
// The sim (possibly on its own thread) fills one after every tick; the main
// thread only ever reads published snapshots, never the World. Fields the core
// World does not have yet are filled with neutral defaults from RunStatics and
// marked TODO(M1.x): wiring a new core system means changing fill_snapshot()
// and nothing else in the binding.
#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/world.h"

namespace orbitfall_binding {

// Per-run values that come from content and do not change tick to tick.
// Written on the main thread only while the sim is idle.
struct RunStatics {
  int32_t waveCount = 0;
  float planetRadius = 0.0f;
  float planetHpMax = 0.0f;
  int32_t slotCount = 0;
  float startStardust = 0.0f;
  float sunStart = 0.0f;       // rad
  float beamWidth = 0.0f;      // world units
  float beamStrength = 0.0f;   // sun baseStrength
};

struct Snapshot {
  // --- run ---
  uint32_t tick = 0;
  float dt = 0.0f;        // scaled dt of the tick that produced this snapshot
  double simTime = 0.0;
  float timeScale = 1.0f;
  int32_t phase = 0;      // 0 intermission, 1 wave, 2 won, 3 lost
  int32_t waveIndex = 0;  // 0-based, next or current
  float stardust = 0.0f;
  float planetHp = 0.0f;
  // --- planet ---
  float planetRotation = 0.0f;  // rad
  float planetAngVel = 0.0f;    // rad/s
  std::vector<int32_t> slotBuildings;  // building index or -1, one per slot
  std::vector<int32_t> slotFlags;      // bit0 lit, bit1 shaded, bit2 has target in arc
  // --- sun ---
  float sunBearing = 0.0f;
  float beamWidth = 0.0f;
  float beamStrength = 0.0f;
  float incomePerSecond = 0.0f;
  float litFraction = 0.0f;
  std::vector<float> shadeIntervals;  // pairs of world angles, merged
  // --- pools (copies of the live part of each field) ---
  uint32_t enemyCount = 0;
  uint32_t enemyOverflow = 0;
  std::vector<float> enemyPosX, enemyPosY, enemyVelX, enemyVelY, enemyHp;
  std::vector<uint32_t> enemyTraits;
  std::vector<uint16_t> enemyType;
  uint32_t projectileCount = 0;
  uint32_t projectileOverflow = 0;
  std::vector<float> projPosX, projPosY, projVelX, projVelY;
  std::vector<uint16_t> projType;
};

namespace detail {
template <typename T>
inline void copyField(std::vector<T>& dst, const T* src, uint32_t count) {
  dst.resize(count);  // allocates only while a buffer is still growing
  if (count) std::memcpy(dst.data(), src, sizeof(T) * static_cast<size_t>(count));
}
}  // namespace detail

inline void fill_snapshot(const orbitfall::World& w, const RunStatics& run, Snapshot& s) {
  s.tick = w.tick;
  s.dt = w.dt;
  s.simTime = w.simTime;
  s.timeScale = w.timeScale;

  s.phase = 0;                     // TODO(M1.9): world phase (intermission/wave/won/lost)
  s.waveIndex = 0;                 // TODO(M1.9): world wave index
  s.stardust = run.startStardust;  // TODO(M1.7): world stardust
  s.planetHp = run.planetHpMax;    // TODO(M1.5): world planet HP

  s.planetRotation = 0.0f;  // TODO(M1.1): world planet rotation
  s.planetAngVel = 0.0f;    // TODO(M1.1): world planet angular velocity
  // TODO(M1.3): copy the slot array (building index per slot).
  s.slotBuildings.assign(static_cast<size_t>(run.slotCount), -1);
  // TODO(M1.7/M1.8/M1.6): lit, shaded and has-target bits per slot.
  s.slotFlags.assign(static_cast<size_t>(run.slotCount), 0);

  s.sunBearing = run.sunStart;       // TODO(M1.7): SunPath bearing at this tick
  s.beamWidth = run.beamWidth;       // TODO(M1.7): live beam width (tunable)
  s.beamStrength = run.beamStrength; // TODO(M1.7): baseStrength x wave lightMult
  s.incomePerSecond = 0.0f;          // TODO(M1.7): income system output
  s.litFraction = 0.0f;              // TODO(M1.7): lit unshaded panels / panels
  s.shadeIntervals.clear();          // TODO(M1.8): merged shadow intervals

  const orbitfall::EnemyPool& e = w.enemies;
  s.enemyCount = e.count;
  s.enemyOverflow = e.overflowCount;
  detail::copyField(s.enemyPosX, e.posX, e.count);
  detail::copyField(s.enemyPosY, e.posY, e.count);
  detail::copyField(s.enemyVelX, e.velX, e.count);
  detail::copyField(s.enemyVelY, e.velY, e.count);
  detail::copyField(s.enemyHp, e.hp, e.count);
  detail::copyField(s.enemyTraits, e.traits, e.count);
  detail::copyField(s.enemyType, e.type, e.count);

  const orbitfall::ProjectilePool& p = w.projectiles;
  s.projectileCount = p.count;
  s.projectileOverflow = p.overflowCount;
  detail::copyField(s.projPosX, p.posX, p.count);
  detail::copyField(s.projPosY, p.posY, p.count);
  detail::copyField(s.projVelX, p.velX, p.count);
  detail::copyField(s.projVelY, p.velY, p.count);
  detail::copyField(s.projType, p.type, p.count);
}

}  // namespace orbitfall_binding
