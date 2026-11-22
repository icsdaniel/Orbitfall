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
#include "systems/focus.h"
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

namespace {

// M2.11: beamTarget of a Lens whose power goes into a merged beam.
constexpr uint32_t kMergedTarget = 0xFFFFFFFEu;

bool isBeamTower(const BuildingDef& def) {
  return def.kind == BuildingKind::Tower && def.hasProjectile && def.projectile.kind == ProjectileKind::Beam;
}

// Share of a beam's damage that lands on `target` (camo: a beam cannot miss,
// so an undetected one loses the expected share).
float beamHitShare(const ContentDB& db, const BuildingSlot& b, uint32_t traits) {
  float miss = 0.0f;
  for (int bit = 0; bit < kTraitCount; ++bit)
    if ((traits & (1u << bit)) && db.damage.traits[bit].missChance > miss) miss = db.damage.traits[bit].missChance;
  return 1.0f - miss * (1.0f - b.detect);
}

// M2.11 Lens merge (BRIEF §4.4): two or more beam towers feeding one focus
// zone merge into one beam from the zone outward, along the mean of their
// axes, with their summed power, piercing every enemy on it (the zone
// included). It fires only while something is on it; otherwise the Lenses
// keep their own beams. Feeders that merged get BuildingSlot::beamTarget ==
// kMergedTarget (their ramp keeps climbing while they stay merged).
void mergeLensBeams(World& world, float dt) {
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  const float pad = world.maxEnemyRadius;
  for (uint32_t f = 0; f < world.focus.size(); ++f) {
    const FocusPoint& fp = world.focus[f];
    if (fp.lenses < 2) continue;
    // The point's Lens feeders that have not merged elsewhere this tick.
    uint16_t lens[kMaxFocusFeeders];
    int count = 0;
    for (int k = 0; k < fp.feeders; ++k) {
      const BuildingSlot& b = world.site(fp.sites[k]);
      if (b.type < 0 || !isBeamTower(db.buildings[static_cast<size_t>(b.type)])) continue;
      if (b.flags & kSlotLensMerged) continue;
      lens[count++] = fp.sites[k];
    }
    if (count < 2) continue;
    float ox = 0.0f, oy = 0.0f;
    focusWorldPos(world, f, ox, oy);
    float dx = 0.0f, dy = 0.0f, reach = 1.0e9f;
    for (int k = 0; k < count; ++k) {
      const BuildingSlot& b = world.site(lens[k]);
      const TowerAim aim = towerAim(world, lens[k]);
      dx += aim.ax;
      dy += aim.ay;
      const float range = slotStats(db.buildings[static_cast<size_t>(b.type)], b).range;
      const float left = range - std::sqrt((ox - aim.x) * (ox - aim.x) + (oy - aim.y) * (oy - aim.y));
      if (left < reach) reach = left;
    }
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0e-3f || !(reach > 0.0f)) continue;
    dx /= len;
    dy /= len;
    // Enemies on the merged beam: from the near edge of the zone out to `reach`.
    const float from = -fp.radius;
    const float ex0 = ox + dx * from, ey0 = oy + dy * from;
    const float ex1 = ox + dx * reach, ey1 = oy + dy * reach;
    const auto onBeam = [&](uint32_t i, float& t) {
      if (e.flags[i] & kEnemyDead) return false;
      const float rx = e.posX[i] - ox, ry = e.posY[i] - oy;
      t = rx * dx + ry * dy;
      if (t < from || t > reach) return false;
      const float side = rx * dy - ry * dx;
      const float size = db.enemies[e.type[i]].size;
      return side * side <= size * size;
    };
    float far = 0.0f;
    bool any = false;
    const float minX = (ex0 < ex1 ? ex0 : ex1) - pad, maxX = (ex0 > ex1 ? ex0 : ex1) + pad;
    const float minY = (ey0 < ey1 ? ey0 : ey1) - pad, maxY = (ey0 > ey1 ? ey0 : ey1) + pad;
    world.grid.query(minX, minY, maxX, maxY, [&](uint32_t i) {
      float t = 0.0f;
      if (!onBeam(i, t)) return;
      any = true;
      if (t > far) far = t;
    });
    if (!any) continue;
    // Summed power of the feeders; each one's beam is drawn up to the zone.
    float dps = 0.0f, power = 0.0f;
    bool over = false;
    const uint32_t lead = lens[0];
    for (int k = 0; k < count; ++k) {
      BuildingSlot& b = world.site(lens[k]);
      const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
      if (b.beamTarget != kMergedTarget) {
        b.ramp = 0.0f;
        b.beamTarget = kMergedTarget;
      }
      b.ramp += dt;
      b.flags |= kSlotLensMerged;
      const BuildingStats stats = slotStats(def, b);
      const bool o = b.activeTime > 0.0f;
      const float p = rampFactor(def, b.ramp) * channelFactor(def, world.channel) * (o ? stats.activePower : 1.0f);
      dps += stats.damage * p;
      power += p;
      over = over || o;
      const TowerAim aim = towerAim(world, lens[k]);
      BeamShot shot;
      shot.site = lens[k];
      shot.x0 = aim.x;
      shot.y0 = aim.y;
      shot.x1 = ox;
      shot.y1 = oy;
      shot.power = p;
      shot.overcharged = o;
      world.beams.push_back(shot);  // reserved (2 per site): no allocation
    }
    BeamShot merged;
    merged.site = static_cast<uint16_t>(lead);
    merged.x0 = ox;
    merged.y0 = oy;
    merged.x1 = ox + dx * (far > fp.radius ? far : fp.radius);
    merged.y1 = oy + dy * (far > fp.radius ? far : fp.radius);
    merged.power = power;
    merged.overcharged = over;
    merged.merged = true;
    world.beams.push_back(merged);
    // Damage: the lead Lens's definition sets falloff and type; distance is
    // counted from the lead tower.
    const BuildingSlot& lb = world.site(lead);
    const BuildingDef& ldef = db.buildings[static_cast<size_t>(lb.type)];
    const BuildingStats lstats = slotStats(ldef, lb);
    const TowerAim laim = towerAim(world, lead);
    const float base = std::sqrt((ox - laim.x) * (ox - laim.x) + (oy - laim.y) * (oy - laim.y));
    world.grid.query(minX, minY, maxX, maxY, [&](uint32_t i) {
      float t = 0.0f;
      if (!onBeam(i, t)) return;
      const float fraction = lstats.range > 0.0f ? (base + t) / lstats.range : 0.0f;
      const float damage = dps * dt * beamHitShare(db, lb, e.traits[i]) *
                           hitMultiplier(db, ldef, db.enemies[e.type[i]], fraction, false);  // M2.4: a beam is never precision
      damageEnemy(world, i, damage, ldef.projectile.type, lb.shieldDamageMult);
    });
  }
}

}  // namespace

void beamFire(World& world, float dt) {
  world.beams.clear();
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) world.site(s).flags &= static_cast<uint8_t>(~kSlotLensMerged);
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  if (db.rules.focus.lensMerge && !world.focus.empty()) mergeLensBeams(world, dt);  // M2.11
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    if (b.flags & kSlotLensMerged) continue;  // M2.11: its power went into a merged beam
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
    const float hitShare = beamHitShare(db, b, e.traits[target]);
    const float fraction = stats.range > 0.0f ? dist / stats.range : 0.0f;
    // M2.11: a target inside a focus zone this Lens feeds takes the focus bonus.
    const float focus = focusBonusAt(world, s, e.posX[target], e.posY[target]);
    const float damage =
        stats.damage * dt * power * hitShare * focus *
        hitMultiplier(db, def, enemy, fraction, false);  // M2.4: a beam hits a Bulwark's body
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
