// M2.6 towers batch 1: the Sun Lens beam (continuous damage that ramps on one
// target and scales with the channel value) and active abilities (Overcharge)
// with a light-charged cooldown. Flak, Railgun, Cryo, Radar and Amplifier are
// content on the shared projectile / aura code (combat.cpp, buildings.cpp).
#include "systems/towers.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "systems/systems.h"
#include "systems/upgrades.h"

namespace orbitfall {

float channelFactor(const BuildingDef& def, float channel) {
  const float floor = clampf(def.beam.channelFloor, 0.0f, 1.0f);
  return floor + (1.0f - floor) * clampf(channel, 0.0f, 1.0f);
}

float rampFactor(const BuildingDef& def, float seconds) {
  const float start = clampf(def.beam.rampStart, 0.0f, 1.0f);
  if (!(def.beam.rampSeconds > 0.0f)) return 1.0f;
  return start + (1.0f - start) * clampf(seconds / def.beam.rampSeconds, 0.0f, 1.0f);
}

float panelChannel(const World& world) {
  if (world.content == nullptr) return 0.0f;
  float total = 0.0f, lit = 0.0f;
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) {
    const BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
    if (def.kind != BuildingKind::Panel) continue;
    const float capacity = effectiveStats(def, b.tiers).income * b.incomeMult;
    total += capacity;
    if ((b.flags & (kSlotLit | kSlotShaded)) == kSlotLit) lit += capacity;
  }
  return total > 0.0f ? lit / total : 0.0f;
}

void beamFire(World& world, float dt) {
  world.beams.clear();
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
    if (def.kind != BuildingKind::Tower || !def.hasProjectile ||
        def.projectile.kind != ProjectileKind::Beam) {
      continue;
    }
    const uint32_t target = world.slotTargets[s];
    if (target == kInvalidIndex || target >= e.count || (e.flags[target] & kEnemyDead)) {
      b.ramp = 0.0f;
      b.beamTarget = kInvalidIndex;
      continue;
    }
    if (target != b.beamTarget) {  // a new target: the ramp starts over
      b.ramp = 0.0f;
      b.beamTarget = target;
    }
    b.ramp += dt;
    const BuildingStats stats = slotStats(def, b);
    const TowerAim aim = towerAim(world, s);
    const float dx = e.posX[target] - aim.x, dy = e.posY[target] - aim.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const EnemyDef& enemy = db.enemies[e.type[target]];
    const bool over = b.activeTime > 0.0f;
    const float power = rampFactor(def, b.ramp) * channelFactor(def, world.channel) *
                        (over ? stats.activePower : 1.0f);
    // Camo: a beam cannot miss, so an undetected one loses the expected share.
    float miss = 0.0f;
    for (int bit = 0; bit < kTraitCount; ++bit)
      if ((e.traits[target] & (1u << bit)) && db.damage.traits[bit].missChance > miss)
        miss = db.damage.traits[bit].missChance;
    const float hitShare = 1.0f - miss * (1.0f - b.detect);
    const float fraction = stats.range > 0.0f ? dist / stats.range : 0.0f;
    const float damage =
        stats.damage * dt * power * hitShare * damageMultiplier(db, def, enemy, fraction);
    BeamShot shot;
    shot.site = static_cast<uint16_t>(s);
    shot.x0 = aim.x;
    shot.y0 = aim.y;
    shot.x1 = e.posX[target];
    shot.y1 = e.posY[target];
    shot.power = power;
    shot.overcharged = over;
    world.beams.push_back(shot);  // reserved per site in initWorld: no allocation
    damageEnemy(world, target, damage, def.projectile.type, b.shieldDamageMult);
  }
}

void abilities(World& world, float dt) {
  if (!world.running() || world.content == nullptr) return;
  const ContentDB& db = *world.content;
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    if (!db.buildings[static_cast<size_t>(b.type)].hasActive) continue;
    if (b.activeTime > 0.0f) {
      b.activeTime -= dt;
      if (b.activeTime < 0.0f) b.activeTime = 0.0f;
    }
    if (b.activeCooldown > 0.0f) {
      // PLAN 5.4 light-charged actives: faster while lit and unshaded.
      const bool lit = (b.flags & (kSlotLit | kSlotShaded)) == kSlotLit;
      b.activeCooldown -= dt * (lit ? db.rules.lightChargeMult : 1.0f);
      if (b.activeCooldown < 0.0f) b.activeCooldown = 0.0f;
    }
  }
}

bool activateBuilding(World& world, int32_t slot) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const float type = static_cast<float>(static_cast<int>(CommandType::ActivateBuilding));
  RejectReason reason = RejectReason::None;
  if (slot < 0 || slot >= static_cast<int32_t>(world.siteCount())) {
    reason = RejectReason::InvalidSlot;
  } else {
    const int16_t anchor = world.site(static_cast<uint32_t>(slot)).anchor;
    if (anchor < 0 || world.site(static_cast<uint32_t>(anchor)).type < 0) {
      reason = RejectReason::EmptySlot;
    } else {
      slot = anchor;
      BuildingSlot& b = world.site(static_cast<uint32_t>(anchor));
      const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
      if (!def.hasActive) {
        reason = RejectReason::NotAllowed;
      } else if (b.activeCooldown > 0.0f || b.activeTime > 0.0f) {
        reason = RejectReason::ActiveNotReady;
      } else {
        const BuildingStats stats = effectiveStats(def, b.tiers);
        b.activeTime = def.active.seconds;
        b.activeCooldown = stats.activeCooldown;
        world.emit(EventType::BuildingActivated, slot, b.type, 0.0f, 0.0f, stats.activeCooldown);
        return true;
      }
    }
  }
  world.emit(EventType::CommandRejected, slot, static_cast<int32_t>(reason), type, 0.0f, 0.0f);
  return false;
}

}  // namespace orbitfall
