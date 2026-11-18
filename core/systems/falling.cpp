// M2.10 falling objects and shields. See systems/falling.h.
#include "systems/falling.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"

namespace orbitfall {

uint32_t spawnFalling(World& world, int type, float bearing, float dist) {
  if (world.content == nullptr || type < 0 || type >= static_cast<int>(world.content->falling.size())) {
    return kInvalidIndex;
  }
  const FallingDef& def = world.content->falling[static_cast<size_t>(type)];
  const uint32_t i = world.falling.add();
  if (i == kInvalidIndex) return kInvalidIndex;
  FallingPool& f = world.falling;
  const float dx = std::cos(bearing), dy = std::sin(bearing);
  f.bearing[i] = wrapTau(bearing);
  f.dist[i] = dist;
  f.posX[i] = dx * dist;
  f.posY[i] = dy * dist;
  f.velX[i] = -dx * def.speed;
  f.velY[i] = -dy * def.speed;
  f.type[i] = static_cast<uint16_t>(type);
  return i;
}

float fallingEta(const World& world, uint32_t i) {
  if (world.content == nullptr || world.planet == nullptr || i >= world.falling.count) return 0.0f;
  const FallingDef& def = world.content->falling[world.falling.type[i]];
  const float left = world.falling.dist[i] - world.planet->radius;
  return left > 0.0f && def.speed > 0.0f ? left / def.speed : 0.0f;
}

float shieldDomeRadius(const World& world, const BuildingDef& def) {
  return (world.planet ? world.planet->radius : 0.0f) + def.shield.height;
}

namespace {

// The shield (slot) that catches an object at (bearing, dist): online, inside
// its arc and under its dome; most energy first, lowest slot on a tie. `dist`
// < 0 skips the dome test.
int coveringShield(const World& world, float bearing, float dist) {
  const ContentDB& db = *world.content;
  int best = -1;
  float bestEnergy = -1.0f;
  const uint32_t n = world.siteCount();  // both rings (M2.1)
  for (uint32_t s = 0; s < n; ++s) {
    const BuildingSlot& b = world.site(s);
    if (b.type < 0 || (b.flags & kSlotShieldOffline) != 0) continue;
    const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
    if (def.kind != BuildingKind::Shield) continue;
    if (dist >= 0.0f && dist > shieldDomeRadius(world, def)) continue;
    const float centre = world.siteAngle(s) + world.planetRotation;
    if (std::fabs(wrapPi(bearing - centre)) > 0.5f * def.shield.width) continue;
    if (b.energy > bestEnergy) {
      bestEnergy = b.energy;
      best = static_cast<int>(s);
    }
  }
  return best;
}

}  // namespace

int shieldCovering(const World& world, float bearing) {
  if (world.content == nullptr || world.planet == nullptr) return -1;
  return coveringShield(world, bearing, -1.0f);
}

void fallingObjects(World& world, float dt) {
  if (!world.running() || !(dt > 0.0f)) return;
  const ContentDB& db = *world.content;
  const float radius = world.planet->radius;
  const float warn = db.rules.fallingWarnSeconds;
  FallingPool& f = world.falling;
  for (uint32_t i = 0; i < f.count; ++i) {
    const FallingDef& def = db.falling[f.type[i]];
    f.dist[i] -= def.speed * dt;
    const float dx = std::cos(f.bearing[i]), dy = std::sin(f.bearing[i]);
    f.posX[i] = dx * f.dist[i];
    f.posY[i] = dy * f.dist[i];
    if ((f.flags[i] & kFallingWarned) == 0 && fallingEta(world, i) <= warn) {
      f.flags[i] |= kFallingWarned;
      world.emit(EventType::FallingWarning, f.type[i], shieldCovering(world, f.bearing[i]), dx * radius,
                 dy * radius, fallingEta(world, i));
    }
  }
}

void shields(World& world, float dt) {
  if (!world.running() || !(dt > 0.0f)) return;
  const ContentDB& db = *world.content;
  const uint32_t n = world.siteCount();  // both rings (M2.1)

  // Recharge, regeneration and upkeep.
  float maxDome = 0.0f;
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
    if (def.kind != BuildingKind::Shield) continue;
    const bool lit = (b.flags & (kSlotLit | kSlotShaded)) == kSlotLit;
    const float rate = dt * (lit ? db.rules.lightChargeMult : 1.0f);
    if ((b.flags & kSlotShieldOffline) != 0) {
      b.charge += rate;
      if (b.charge >= def.shield.rechargeSeconds) {
        b.charge = 0.0f;
        b.energy = def.shield.energy;
        b.flags &= static_cast<uint8_t>(~kSlotShieldOffline);
        world.emit(EventType::ShieldOnline, static_cast<int32_t>(s));
      }
    } else {
      b.energy = b.energy + def.shield.regenPerSec * rate;
      if (b.energy > def.shield.energy) b.energy = def.shield.energy;
      if (def.shield.upkeepPerSec > 0.0f) {
        const float cost = def.shield.upkeepPerSec * dt;
        world.stardust -= cost < world.stardust ? cost : world.stardust;
      }
    }
    if ((b.flags & kSlotShieldOffline) == 0) {
      const float dome = shieldDomeRadius(world, def);
      if (dome > maxDome) maxDome = dome;
    }
  }
  if (!(maxDome > 0.0f)) return;

  // Absorption: every live object under some online dome.
  FallingPool& f = world.falling;
  for (uint32_t i = 0; i < f.count; ++i) {
    if ((f.flags[i] & kFallingDead) != 0 || f.dist[i] > maxDome) continue;
    const int slot = coveringShield(world, f.bearing[i], f.dist[i]);
    if (slot < 0) continue;
    BuildingSlot& b = world.site(static_cast<uint32_t>(slot));
    const FallingDef& def = db.falling[f.type[i]];
    f.flags[i] |= kFallingDead;
    b.energy -= def.damage;
    if (b.energy <= 0.0f) {
      b.energy = 0.0f;
      b.charge = 0.0f;
      b.flags |= kSlotShieldOffline;
    }
    world.emit(EventType::ShieldAbsorbed, slot, f.type[i], f.posX[i], f.posY[i], b.energy);
    if ((b.flags & kSlotShieldOffline) != 0) world.emit(EventType::ShieldOffline, slot);
  }
}

void fallingImpact(World& world, float /*dt*/) {
  if (world.content == nullptr || world.planet == nullptr) return;
  FallingPool& f = world.falling;
  if (world.running()) {
    const ContentDB& db = *world.content;
    const float radius = world.planet->radius;
    for (uint32_t i = 0; i < f.count; ++i) {
      if ((f.flags[i] & kFallingDead) != 0 || f.dist[i] > radius) continue;
      const FallingDef& def = db.falling[f.type[i]];
      f.flags[i] |= kFallingDead;
      // Planet HP only: no building is ever damaged.
      world.planetHp -= def.damage;
      if (world.planetHp < 0.0f) world.planetHp = 0.0f;
      world.emit(EventType::FallingImpact, f.type[i], 0, std::cos(f.bearing[i]) * radius,
                 std::sin(f.bearing[i]) * radius, def.damage);
      if (world.planetHp <= 0.0f && world.phase != Phase::Lost) {
        world.phase = Phase::Lost;
        world.emit(EventType::GameOver, static_cast<int32_t>(world.waveIndex));
      }
    }
  }
  for (uint32_t i = 0; i < f.count;) {
    if ((f.flags[i] & kFallingDead) != 0) {
      f.remove(i);
    } else {
      ++i;
    }
  }
}

}  // namespace orbitfall
