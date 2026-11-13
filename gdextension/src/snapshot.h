#pragma once
// Snapshot: the plain-data copy of the sim state that SimServer's getters read.
//
// The sim (possibly on its own thread) fills one after every tick; the main
// thread only ever reads published snapshots, never the World. Wiring a new
// core system means changing fill_snapshot() and little else in the binding.
//
// Render convention (one for core, binding, client and mock): the instance
// basis is the entity's RADIUS in world units, so batches use a shape of
// radius 1 (BatchRenderer shape size 2); the shape's "up" points along the
// velocity. The buffers come from core's fillEnemyRenderBuffer /
// fillProjectileRenderBuffer at alpha 1; the main thread only steps the
// origins back along the velocity for interpolation.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/render_snapshot.h"
#include "sim/world.h"
#include "systems/enemies.h"
#include "systems/shadows.h"

namespace orbitfall_binding {

// Per-run values that do not change tick to tick. Written on the main thread
// only while the sim is idle.
struct RunStatics {
  int32_t waveCount = 0;
  float planetRadius = 0.0f;
  float planetHpMax = 0.0f;
  int32_t slotCount = 0;
  // RGBA per enemy definition (content order) from the client palette; may be null.
  const float* enemyColors = nullptr;
  float projectileColor[4] = {1.0f, 0.92f, 0.55f, 1.0f};
};

// Carrier-size and up: the few units that get an HP bar (readability rule 7).
struct LargeUnit {
  float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
  float radius = 0.0f;
  float hpFrac = 1.0f;
  bool shadow = false;  // casting a shadow on the planet right now
};

inline constexpr uint32_t kMaxShadePairs = 256;

struct Snapshot {
  // --- run ---
  uint32_t tick = 0;
  float dt = 0.0f;        // scaled dt of the tick that produced this snapshot
  double simTime = 0.0;
  float timeScale = 1.0f;
  bool running = false;   // wave clock running: entities move, so reads interpolate
  int32_t phase = 0;      // 0 intermission, 1 wave, 2 won, 3 lost
  int32_t waveIndex = 0;  // 0-based, next or current
  float waveTime = 0.0f;
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
  float litFraction = 0.0f;           // lit unshaded panels / panels
  std::vector<float> shadeIntervals;  // pairs of world angles, merged
  // --- render buffers at alpha 1 (16 floats per instance) plus velocities ---
  uint32_t enemyCount = 0;
  uint32_t enemyOverflow = 0;
  std::vector<float> enemyRender, enemyVelX, enemyVelY;
  uint32_t projectileCount = 0;
  uint32_t projectileOverflow = 0;
  std::vector<float> projRender, projVelX, projVelY;
  std::vector<LargeUnit> largeUnits;
};

namespace detail {
template <typename T>
inline void copyField(std::vector<T>& dst, const T* src, uint32_t count) {
  dst.resize(count);  // allocates only while a buffer is still growing
  if (count) std::memcpy(dst.data(), src, sizeof(T) * static_cast<size_t>(count));
}
}  // namespace detail

inline void fill_snapshot(const orbitfall::World& w, const RunStatics& run, Snapshot& s) {
  using namespace orbitfall;
  s.tick = w.tick;
  s.dt = w.dt;
  s.simTime = w.simTime;
  s.timeScale = w.timeScale;
  s.running = w.running();

  s.phase = static_cast<int32_t>(w.phase);
  s.waveIndex = static_cast<int32_t>(w.waveIndex);
  s.waveTime = w.waveTime;
  s.stardust = w.stardust;
  s.planetHp = w.planetHp;

  s.planetRotation = w.planetRotation;
  s.planetAngVel = w.planetAngVel;
  const size_t slots = w.slots.size();
  s.slotBuildings.resize(slots);
  s.slotFlags.resize(slots);
  uint32_t panels = 0, earning = 0;
  for (size_t i = 0; i < slots; ++i) {
    const BuildingSlot& b = w.slots[i];
    s.slotBuildings[i] = b.type;
    s.slotFlags[i] = b.flags;
    if (b.type < 0 || w.content == nullptr) continue;
    if (w.content->buildings[static_cast<size_t>(b.type)].kind != BuildingKind::Panel) continue;
    ++panels;
    if ((b.flags & kSlotLit) != 0 && (b.flags & kSlotShaded) == 0) ++earning;
  }

  s.sunBearing = w.sunBearing;
  s.beamWidth = w.sun ? w.sun->beamWidth : 0.0f;
  s.beamStrength = w.beamStrength;
  s.incomePerSecond = w.incomePerSecond;
  s.litFraction = panels > 0 ? static_cast<float>(earning) / static_cast<float>(panels) : 0.0f;
  const uint32_t pairsMax = w.shadeCount < kMaxShadePairs ? w.shadeCount : kMaxShadePairs;
  s.shadeIntervals.resize(static_cast<size_t>(pairsMax) * 2);
  const uint32_t pairs =
      pairsMax > 0 && w.planet != nullptr ? shadeIntervalAngles(w, s.shadeIntervals.data(), pairsMax) : 0;
  s.shadeIntervals.resize(static_cast<size_t>(pairs) * 2);

  RenderParams params;  // alpha 1: the latest tick; the main thread interpolates
  params.enemyColors = run.enemyColors;
  for (int c = 0; c < 4; ++c) params.projectileColor[c] = run.projectileColor[c];

  const EnemyPool& e = w.enemies;
  s.enemyOverflow = e.overflowCount;
  s.enemyRender.resize(static_cast<size_t>(e.count) * kRenderFloatsPerInstance);
  s.enemyCount = fillEnemyRenderBuffer(w, s.enemyRender.data(), e.count, params);
  detail::copyField(s.enemyVelX, e.velX, s.enemyCount);
  detail::copyField(s.enemyVelY, e.velY, s.enemyCount);
  // The shader reads custom.w as an outline width override; core puts the
  // layer's hp fraction there, which the placeholder shader has no use for.
  for (uint32_t i = 0; i < s.enemyCount; ++i) s.enemyRender[static_cast<size_t>(i) * 16 + 15] = 0.0f;

  s.largeUnits.clear();
  if (w.content != nullptr && w.planet != nullptr) {
    const float sx = std::cos(w.sunBearing), sy = std::sin(w.sunBearing);
    for (uint32_t i = 0; i < s.enemyCount; ++i) {
      const EnemyDef& def = w.content->enemies[e.type[i]];
      if (!def.hpBar) continue;
      LargeUnit u;
      u.x = e.posX[i];
      u.y = e.posY[i];
      u.vx = e.velX[i];
      u.vy = e.velY[i];
      u.radius = def.size;
      const float frac = def.hp > 0.0f ? e.hp[i] / def.hp : 0.0f;
      u.hpFrac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
      ShadeInterval ignored;
      u.shadow = def.shadowCaster && casterInterval(u.x, u.y, def.size, sx, sy, w.planet->radius, ignored);
      s.largeUnits.push_back(u);
    }
  }

  const ProjectilePool& p = w.projectiles;
  s.projectileOverflow = p.overflowCount;
  s.projRender.resize(static_cast<size_t>(p.count) * kRenderFloatsPerInstance);
  s.projectileCount = fillProjectileRenderBuffer(w, s.projRender.data(), p.count, params);
  detail::copyField(s.projVelX, p.velX, s.projectileCount);
  detail::copyField(s.projVelY, p.velY, s.projectileCount);
  for (uint32_t i = 0; i < s.projectileCount; ++i) s.projRender[static_cast<size_t>(i) * 16 + 15] = 0.0f;
}

}  // namespace orbitfall_binding
