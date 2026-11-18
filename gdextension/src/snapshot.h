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
#include "systems/falling.h"
#include "systems/shadows.h"

namespace orbitfall_binding {

// Per-run values that do not change tick to tick. Written on the main thread
// only while the sim is idle.
struct RunStatics {
  int32_t waveCount = 0;
  float planetRadius = 0.0f;
  float planetHpMax = 0.0f;
  int32_t slotCount = 0;
  // M2.1
  int32_t layerCount = 1;
  float elevatedHeight = 0.0f;
  // RGBA per enemy definition (content order) from the client palette; may be null.
  const float* enemyColors = nullptr;
  float projectileColor[4] = {1.0f, 0.92f, 0.55f, 1.0f};
  const float* fallingColors = nullptr;  // M2.10: RGBA per falling definition; may be null
};

// --- M2.10 falling objects and shields ---
// One placed shield: where its arc is, how much energy, recharge progress.
struct ShieldView {
  int32_t slot = 0;
  float energy = 0.0f, energyMax = 0.0f;
  float chargeFrac = 0.0f;  // recharge progress while offline, 0..1
  bool online = true;
  bool lit = false;         // charging lightChargeMult times faster
  float halfWidth = 0.0f;   // rad
  float domeRadius = 0.0f;  // world units from the planet centre
  float localAngle = 0.0f;  // planet-local angle of the site (footprint centre)
};
// One telegraphed falling object (within fallingWarnSeconds of impact).
struct FallingWarningView {
  float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;  // object position / velocity
  float bearing = 0.0f;  // world rad of the impact point
  float eta = 0.0f;      // seconds to impact (sim time)
  float damage = 0.0f;
  int32_t type = 0;      // falling definition index
  int32_t shieldSlot = -1;  // the online shield covering its bearing now, or -1
};
// --- end M2.10 ---

// Carrier-size and up: the few units that get an HP bar (readability rule 7).
struct LargeUnit {
  float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
  float radius = 0.0f;
  float hpFrac = 1.0f;
  bool shadow = false;  // casting a shadow on the planet right now
  // M2.3: segmented HP bar and the shield still up, as a fraction of the full shield.
  int32_t segments = 1;
  float shieldFrac = 0.0f;
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
  // M2.1: one entry per site (ground ring, then the elevated ring).
  std::vector<int32_t> siteBuildings;  // building index on anchors, else -1
  std::vector<int32_t> siteFlags;
  std::vector<int32_t> siteOwners;     // anchor site of the building covering the slot, -1 = free
  std::vector<float> siteModifiers;    // 4 per site: range, fire rate, damage, income multipliers
  std::vector<int32_t> auraLinks;      // triples: source site, target site, stat
  std::vector<int32_t> slotTiers;      // M2.2: kUpgradePaths entries per site, bought tiers
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
  // --- M2.10 ---
  uint32_t fallingCount = 0;
  uint32_t fallingOverflow = 0;
  std::vector<float> fallingRender, fallingVelX, fallingVelY;
  std::vector<ShieldView> shields;
  std::vector<FallingWarningView> warnings;
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
  const size_t sites = w.siteCount();
  s.slotTiers.resize(sites * static_cast<size_t>(kUpgradePaths));  // M2.2
  s.siteBuildings.resize(sites);
  s.siteFlags.resize(sites);
  s.siteOwners.resize(sites);
  s.siteModifiers.resize(sites * 4);
  s.auraLinks.resize(w.auraLinks.size() * 3);
  for (size_t i = 0; i < w.auraLinks.size(); ++i) {
    s.auraLinks[3 * i] = w.auraLinks[i].source;
    s.auraLinks[3 * i + 1] = w.auraLinks[i].target;
    s.auraLinks[3 * i + 2] = w.auraLinks[i].stat;
  }
  for (size_t i = 0; i < sites; ++i) {
    const BuildingSlot& b = w.site(static_cast<uint32_t>(i));
    if (i < slots) {
      s.slotBuildings[i] = b.type;
      s.slotFlags[i] = b.flags;
    }
    s.siteBuildings[i] = b.type;
    s.siteFlags[i] = b.flags;
    s.siteOwners[i] = b.anchor;
    s.siteModifiers[4 * i] = b.rangeMult;
    s.siteModifiers[4 * i + 1] = b.fireRateMult;
    s.siteModifiers[4 * i + 2] = b.damageMult;
    s.siteModifiers[4 * i + 3] = b.incomeMult;
    for (int p = 0; p < kUpgradePaths; ++p)  // M2.2
      s.slotTiers[i * static_cast<size_t>(kUpgradePaths) + static_cast<size_t>(p)] = b.tiers[p];
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
  for (uint32_t i = 0; i < s.enemyCount; ++i) {  // M2.6: interpolate slowed enemies at their real speed
    if (e.slowTime[i] <= 0.0f) continue;
    s.enemyVelX[i] *= speedFactor(e, i);
    s.enemyVelY[i] *= speedFactor(e, i);
  }
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
      u.segments = def.hpBarSegments;
      u.shieldFrac = def.shield > 0.0f ? e.shield[i] / def.shield : 0.0f;
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

  // --- M2.10 falling objects, warnings and shields ---
  params.fallingColors = run.fallingColors;
  const FallingPool& f = w.falling;
  s.fallingOverflow = f.overflowCount;
  s.fallingRender.resize(static_cast<size_t>(f.count) * kRenderFloatsPerInstance);
  s.fallingCount = fillFallingRenderBuffer(w, s.fallingRender.data(), f.count, params);
  detail::copyField(s.fallingVelX, f.velX, s.fallingCount);
  detail::copyField(s.fallingVelY, f.velY, s.fallingCount);
  for (uint32_t i = 0; i < s.fallingCount; ++i) s.fallingRender[static_cast<size_t>(i) * 16 + 15] = 0.0f;
  s.warnings.clear();
  s.shields.clear();
  if (w.content == nullptr || w.planet == nullptr) return;
  for (uint32_t i = 0; i < f.count; ++i) {
    if ((f.flags[i] & kFallingWarned) == 0 || (f.flags[i] & kFallingDead) != 0) continue;
    FallingWarningView v;
    v.x = f.posX[i];
    v.y = f.posY[i];
    v.vx = f.velX[i];
    v.vy = f.velY[i];
    v.bearing = f.bearing[i];
    v.eta = fallingEta(w, i);
    v.damage = w.content->falling[f.type[i]].damage;
    v.type = f.type[i];
    v.shieldSlot = shieldCovering(w, f.bearing[i]);
    s.warnings.push_back(v);
  }
  for (size_t i = 0; i < sites; ++i) {  // both rings (M2.1)
    const BuildingSlot& b = w.site(static_cast<uint32_t>(i));
    if (b.type < 0) continue;
    const BuildingDef& def = w.content->buildings[static_cast<size_t>(b.type)];
    if (def.kind != BuildingKind::Shield) continue;
    ShieldView v;
    v.slot = static_cast<int32_t>(i);
    v.localAngle = w.siteAngle(static_cast<uint32_t>(i));
    v.energy = b.energy;
    v.energyMax = def.shield.energy;
    v.online = (b.flags & kSlotShieldOffline) == 0;
    v.chargeFrac = v.online || !(def.shield.rechargeSeconds > 0.0f) ? 0.0f : b.charge / def.shield.rechargeSeconds;
    v.lit = (b.flags & (kSlotLit | kSlotShaded)) == kSlotLit;
    v.halfWidth = 0.5f * def.shield.width;
    v.domeRadius = shieldDomeRadius(w, def);
    s.shields.push_back(v);
  }
}

}  // namespace orbitfall_binding
