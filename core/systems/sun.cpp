#include "systems/sun.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/enemies.h"
#include "systems/shadows.h"
#include "systems/systems.h"

namespace orbitfall {
namespace {

float constantDrift(const SunDef& def, float bearing, float dt) {
  return wrapTau(bearing + def.speed * dt);
}

float fixedSun(const SunDef& def, float /*bearing*/, float /*dt*/) { return wrapTau(def.start); }

// Indexed by SunPathType.
constexpr SunPath kSunPaths[] = {
    {"constant_drift", constantDrift},
    {"fixed", fixedSun},
};

}  // namespace

const SunPath& sunPathFor(SunPathType type) {
  const size_t i = static_cast<size_t>(type);
  return kSunPaths[i < sizeof(kSunPaths) / sizeof(kSunPaths[0]) ? i : 0];
}

bool inBeam(float planetRadius, float beamWidth, float sunBearing, float worldAngle) {
  const float d = worldAngle - sunBearing;
  return std::cos(d) > 0.0f && std::fabs(planetRadius * std::sin(d)) <= 0.5f * beamWidth;
}

float litArc(float planetRadius, float beamWidth) {
  if (!(planetRadius > 0.0f)) return 0.0f;
  return 2.0f * std::asin(clampf(beamWidth / (2.0f * planetRadius), 0.0f, 1.0f));
}

void sunlight(World& world, float dt) {
  if (world.planet == nullptr || world.sun == nullptr) return;
  if (world.running() && world.sunPath != nullptr) {
    world.sunBearing = world.sunPath->advance(*world.sun, world.sunBearing, dt);
  }
  world.beamStrength = world.sun->baseStrength * world.lightMult;

  const float radius = world.planet->radius, width = world.sun->beamWidth;
  const float sx = std::cos(world.sunBearing), sy = std::sin(world.sunBearing);

  // Shadows (M1.8): one interval per shadow caster, merged.
  uint32_t count = 0;
  if (world.content != nullptr) {
    const EnemyPool& e = world.enemies;
    const uint32_t capacity = static_cast<uint32_t>(world.shade.size());
    for (uint32_t i = 0; i < e.count && count < capacity; ++i) {
      const EnemyDef& def = world.content->enemies[e.type[i]];
      if (!def.shadowCaster || (e.flags[i] & kEnemyDead)) continue;
      if (casterInterval(e.posX[i], e.posY[i], def.size, sx, sy, radius, world.shade[count])) ++count;
    }
  }
  world.shadeCount = mergeIntervals(world.shade.data(), count);

  // Lit and shaded flags are refreshed in every phase: the planet also turns
  // between waves.
  const uint32_t n = world.slotCount();
  uint32_t shadedSlots = 0;
  bool changed = false;
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.slots[s];
    const uint8_t before = b.flags;
    b.flags &= static_cast<uint8_t>(~(kSlotLit | kSlotShaded));
    const float angle = world.slotAngle(s) + world.planetRotation;
    if (inBeam(radius, width, world.sunBearing, angle)) {
      b.flags |= kSlotLit;
      if (isShaded(world.shade.data(), world.shadeCount, radius * std::sin(angle - world.sunBearing))) {
        b.flags |= kSlotShaded;
        ++shadedSlots;
      }
    }
    changed = changed || ((before ^ b.flags) & kSlotShaded) != 0;
  }
  if (changed) {
    world.emit(EventType::ShadowChanged, static_cast<int32_t>(shadedSlots),
               static_cast<int32_t>(world.shadeCount));
  }
}

void income(World& world, float dt) {
  if (world.content == nullptr || world.planet == nullptr) return;
  float rate = 0.0f;
  for (const BuildingSlot& b : world.slots) {
    if (b.type < 0 || (b.flags & (kSlotLit | kSlotShaded)) != kSlotLit) continue;
    const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
    if (def.kind == BuildingKind::Panel) rate += def.panelRate * world.beamStrength;
  }
  world.incomePerSecond = rate;
  if (!world.running() || !(rate > 0.0f) || !(dt > 0.0f)) return;
  const float before = world.stardust;
  world.stardust += rate * dt;
  // The HUD shows whole stardust: one event per unit earned, not one per tick.
  if (std::floor(world.stardust) != std::floor(before)) {
    world.emit(EventType::StardustChanged, 0, 0, world.stardust - before, 0.0f, world.stardust);
  }
}

}  // namespace orbitfall
