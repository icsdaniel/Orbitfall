// M1.6: tower targeting and firing, projectile flight, collisions through the
// spatial grid, layer popping, splash and the kill bounty.
#include "systems/combat.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/buildings.h"
#include "systems/enemies.h"
#include "systems/systems.h"
#include "systems/upgrades.h"

namespace orbitfall {
namespace {

const BuildingDef* towerDef(const World& world, const BuildingSlot& b) {
  if (b.type < 0) return nullptr;
  const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
  if (def.kind != BuildingKind::Tower || !def.hasProjectile) return nullptr;
  return &def;
}

// Hit points left across every layer, for "strong" targeting.
float totalHp(const EnemyDef& def, float hp) { return def.hp - layerHp(def) + hp; }

// Unit direction for a shot of speed `s` from T that meets a target at
// (dx, dy) relative to T moving with velocity (vx, vy). Falls back to aiming
// straight at the target when there is no intercept.
void leadDirection(float dx, float dy, float vx, float vy, float s, float& outX, float& outY) {
  float aimX = dx, aimY = dy;
  const float a = vx * vx + vy * vy - s * s;
  const float b = 2.0f * (dx * vx + dy * vy);
  const float c = dx * dx + dy * dy;
  float t = -1.0f;
  if (std::fabs(a) < 1e-6f) {
    if (std::fabs(b) > 1e-6f) t = -c / b;
  } else {
    const float disc = b * b - 4.0f * a * c;
    if (disc >= 0.0f) {
      const float root = std::sqrt(disc);
      const float t0 = (-b - root) / (2.0f * a), t1 = (-b + root) / (2.0f * a);
      t = t0 > 0.0f && (t0 < t1 || t1 <= 0.0f) ? t0 : t1;
    }
  }
  if (t > 0.0f) {
    aimX = dx + vx * t;
    aimY = dy + vy * t;
  }
  const float len = std::sqrt(aimX * aimX + aimY * aimY);
  if (len > 1e-6f) {
    outX = aimX / len;
    outY = aimY / len;
  }
}

// First contact of the segment A->B with a circle, as a fraction in [0, 1];
// negative when they do not touch.
float segmentCircle(float ax, float ay, float bx, float by, float cx, float cy, float r) {
  const float dx = bx - ax, dy = by - ay;
  const float fx = ax - cx, fy = ay - cy;
  const float c = fx * fx + fy * fy - r * r;
  if (c <= 0.0f) return 0.0f;  // starts inside
  const float a = dx * dx + dy * dy;
  if (a <= 0.0f) return -1.0f;
  const float b = fx * dx + fy * dy;
  if (b >= 0.0f) return -1.0f;  // moving away
  const float disc = b * b - a * c;
  if (disc < 0.0f) return -1.0f;
  const float t = (-b - std::sqrt(disc)) / a;
  return t <= 1.0f ? t : -1.0f;
}

}  // namespace

TowerAim towerAim(const World& world, uint32_t slot) {
  TowerAim aim;
  const float angle = world.siteAngle(slot) + world.planetRotation;
  const float r = world.siteRadius(slot);
  aim.x = r * std::cos(angle);
  aim.y = r * std::sin(angle);
  const float axis = angle + world.site(slot).aimOffset;
  aim.ax = std::cos(axis);
  aim.ay = std::sin(axis);
  return aim;
}

bool canTarget(const TowerAim& aim, float range, float arc, float ex, float ey) {
  const float dx = ex - aim.x, dy = ey - aim.y;
  const float d2 = dx * dx + dy * dy;
  if (d2 > range * range) return false;
  // angle <= arc  <=>  dot >= |d| * cos(arc)
  return dx * aim.ax + dy * aim.ay >= std::sqrt(d2) * std::cos(arc);
}

bool canTarget(const TowerAim& aim, const BuildingDef& def, float ex, float ey, float rangeMult) {
  return canTarget(aim, def.range * rangeMult, def.arc, ex, ey);
}

float falloffAt(const BuildingDef& def, float t) {
  const size_t n = def.falloff.size();
  if (n == 0) return 1.0f;
  if (t <= def.falloff[0].t) return def.falloff[0].mult;
  for (size_t k = 1; k < n; ++k) {
    const FalloffPoint& p0 = def.falloff[k - 1];
    const FalloffPoint& p1 = def.falloff[k];
    if (t <= p1.t) {
      const float span = p1.t - p0.t;
      return span > 0.0f ? p0.mult + (p1.mult - p0.mult) * (t - p0.t) / span : p1.mult;
    }
  }
  return def.falloff[n - 1].mult;
}

float damageFloor(const ContentDB& db) {
  return db.rules.minDamageMult > kHardMinDamageMult ? db.rules.minDamageMult : kHardMinDamageMult;
}

float typeMultiplier(const ContentDB& db, const EnemyDef& enemy, DamageType type) {
  const int t = static_cast<int>(type);
  float mult = enemy.damageMult[t];
  for (int bit = 0; bit < kTraitCount; ++bit)
    if (enemy.traits & (1u << bit)) mult *= db.damage.traits[bit].mult[t];
  const float floor = damageFloor(db);
  return mult < floor ? floor : mult;
}

float shieldMultiplier(const ContentDB& db, DamageType type) {
  const float mult = db.damage.shieldMult[static_cast<int>(type)];
  const float floor = damageFloor(db);
  return mult < floor ? floor : mult;
}

float damageMultiplier(const ContentDB& db, const BuildingDef& source, const EnemyDef& enemy,
                       float rangeFraction) {
  const float mult = falloffAt(source, clampf(rangeFraction, 0.0f, 1.0f)) *
                     typeMultiplier(db, enemy, source.projectile.type);
  const float floor = damageFloor(db);
  return mult < floor ? floor : mult;
}

// --- M2.4 weak points ---
bool isWeakPointHit(const EnemyDef& enemy, float towerAngle, float ex, float ey) {
  if (!enemy.hasWeakPoint) return false;
  const float bearing = std::atan2(ey, ex);
  return std::fabs(wrapPi(towerAngle - bearing)) <= enemy.weakPoint.window;
}

float weakPointFactor(const EnemyDef& enemy, bool weakHit) {
  if (!enemy.hasWeakPoint) return 1.0f;
  return weakHit ? enemy.weakPoint.mult : enemy.weakPoint.bodyMult;
}

float hitMultiplier(const ContentDB& db, const BuildingDef& source, const EnemyDef& enemy,
                    float rangeFraction, bool weakHit) {
  const float mult = damageMultiplier(db, source, enemy, rangeFraction) * weakPointFactor(enemy, weakHit);
  const float floor = damageFloor(db);
  return mult < floor ? floor : mult;
}

bool weakPointHint(const World& world, WeakPointHint& out) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const ContentDB& db = *world.content;
  const EnemyPool& e = world.enemies;
  // The weak-point enemy nearest the planet: the most urgent one.
  uint32_t best = kInvalidIndex;
  float bestD2 = 0.0f;
  for (uint32_t i = 0; i < e.count; ++i) {
    if ((e.flags[i] & kEnemyDead) || !db.enemies[e.type[i]].hasWeakPoint) continue;
    const float d2 = e.posX[i] * e.posX[i] + e.posY[i] * e.posY[i];
    if (best == kInvalidIndex || d2 < bestD2) {
      best = i;
      bestD2 = d2;
    }
  }
  if (best == kInvalidIndex) return false;
  const float bearing = std::atan2(e.posY[best], e.posX[best]);
  int32_t bestSlot = -1;
  float bestTurn = 0.0f;
  const uint32_t n = world.siteCount();  // both rings (M2.1)
  for (uint32_t s = 0; s < n; ++s) {
    const int16_t type = world.site(s).type;
    if (type < 0 || !db.buildings[static_cast<size_t>(type)].precision) continue;
    const float turn = wrapPi(bearing - (world.siteAngle(s) + world.planetRotation));
    if (bestSlot < 0 || std::fabs(turn) < std::fabs(bestTurn)) {
      bestSlot = static_cast<int32_t>(s);
      bestTurn = turn;
    }
  }
  if (bestSlot < 0) return false;
  out.turn = bestTurn;
  out.bearing = bearing;
  out.slot = bestSlot;
  out.enemy = best;
  out.distance = std::sqrt(bestD2);
  return true;
}

bool damageEnemy(World& world, uint32_t i, float damage, DamageType type) {
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  if (i >= e.count || (e.flags[i] & kEnemyDead) || !(damage > 0.0f)) return false;
  if (e.shield[i] > 0.0f) {
    const float sm = shieldMultiplier(db, type);
    const float onShield = damage * sm;
    if (onShield < e.shield[i]) {
      e.shield[i] -= onShield;
      return false;
    }
    damage = (onShield - e.shield[i]) / sm;
    e.shield[i] = 0.0f;
    e.traits[i] &= ~static_cast<uint32_t>(kTraitShielded);
    if (!(damage > 0.0f)) return false;
  }
  // Bounded by the chain length; the loader rejects cyclic chains.
  for (;;) {
    e.hp[i] -= damage;
    if (e.hp[i] > 0.0f) return false;
    damage = -e.hp[i];  // carries into the next layer
    const EnemyDef& def = db.enemies[e.type[i]];
    if (def.popsInto == kNoIndex) {
      e.hp[i] = 0.0f;
      e.flags[i] |= kEnemyDead;
      const float bounty = def.bounty * db.rules.killBounty;
      world.emit(EventType::EnemyKilled, e.type[i], e.lane[i], e.posX[i], e.posY[i], bounty);
      if (bounty > 0.0f) addStardust(world, bounty);
      return true;
    }
    const EnemyDef& next = db.enemies[static_cast<size_t>(def.popsInto)];
    world.emit(EventType::LayerPopped, e.type[i], def.popsInto, e.posX[i], e.posY[i]);
    e.type[i] = static_cast<uint16_t>(def.popsInto);
    e.hp[i] = layerHp(next);
    // The shield belongs to the entity: it carries over to the next layer
    // while that layer is Shielded too, and never comes back once broken.
    if (!(next.traits & kTraitShielded)) e.shield[i] = 0.0f;
    e.traits[i] = e.shield[i] > 0.0f ? next.traits
                                     : next.traits & ~static_cast<uint32_t>(kTraitShielded);
    if (def.speed > 0.0f && next.speed != def.speed) {
      const float scale = next.speed / def.speed;
      e.velX[i] *= scale;
      e.velY[i] *= scale;
    }
    if (!(damage > 0.0f)) return false;
  }
}

void towerTargeting(World& world, float /*dt*/) {
  if (world.content == nullptr || world.planet == nullptr) return;
  const ContentDB& db = *world.content;
  const EnemyPool& e = world.enemies;
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    b.flags &= static_cast<uint8_t>(~kSlotHasTarget);
    world.slotTargets[s] = kInvalidIndex;
    const BuildingDef* def = world.running() ? towerDef(world, b) : nullptr;
    if (def == nullptr) continue;

    const TowerAim aim = towerAim(world, s);
    const BuildingStats stats = slotStats(*def, b);  // M2.2 upgrades x M2.1 elevated/auras
    const TargetingMode mode = static_cast<TargetingMode>(b.mode);
    uint32_t best = kInvalidIndex;
    float bestKey = 0.0f, bestTie = 0.0f;
    world.grid.query(aim.x - stats.range, aim.y - stats.range, aim.x + stats.range,
                     aim.y + stats.range, [&](uint32_t i) {
      if (e.flags[i] & kEnemyDead) return;
      const float ex = e.posX[i], ey = e.posY[i];
      if (!canTarget(aim, stats.range, stats.arc, ex, ey)) return;
      // Lower key wins; ties go to the enemy nearest the planet, then the lower index.
      const float toPlanet = ex * ex + ey * ey;
      float key = toPlanet;
      if (mode == TargetingMode::Strong) {
        key = -totalHp(db.enemies[e.type[i]], e.hp[i]);
      } else if (mode == TargetingMode::Close) {
        key = (ex - aim.x) * (ex - aim.x) + (ey - aim.y) * (ey - aim.y);
      }
      const bool better = best == kInvalidIndex || key < bestKey ||
                          (key == bestKey && (toPlanet < bestTie || (toPlanet == bestTie && i < best)));
      if (better) {
        best = i;
        bestKey = key;
        bestTie = toPlanet;
      }
    });
    if (best != kInvalidIndex) {
      b.flags |= kSlotHasTarget;
      world.slotTargets[s] = best;
    }
  }
}

void towerFire(World& world, float dt) {
  if (!world.running()) return;
  const EnemyPool& e = world.enemies;
  ProjectilePool& p = world.projectiles;
  const uint32_t n = world.siteCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.site(s);
    const BuildingDef* def = towerDef(world, b);
    if (def == nullptr) continue;
    b.cooldown -= dt;
    const uint32_t target = world.slotTargets[s];
    if (target == kInvalidIndex) {
      if (b.cooldown < 0.0f) b.cooldown = 0.0f;  // ready, waiting
      continue;
    }
    if (b.cooldown > 0.0f || !(def->projectile.speed > 0.0f)) continue;
    const BuildingStats stats = slotStats(*def, b);  // M2.2 upgrades x M2.1 elevated/auras
    if (!(stats.fireRate > 0.0f)) continue;
    b.cooldown += 1.0f / stats.fireRate;  // keeps the remainder, so the average rate is exact
    if (b.cooldown < 0.0f) b.cooldown = 0.0f;

    const TowerAim aim = towerAim(world, s);
    float dirX = aim.ax, dirY = aim.ay;
    leadDirection(e.posX[target] - aim.x, e.posY[target] - aim.y, e.velX[target], e.velY[target],
                  def->projectile.speed, dirX, dirY);
    const uint32_t k = p.add();
    if (k == kInvalidIndex) continue;  // pool full: the shot is lost
    p.posX[k] = aim.x;
    p.posY[k] = aim.y;
    p.velX[k] = dirX * def->projectile.speed;
    p.velY[k] = dirY * def->projectile.speed;
    p.damage[k] = stats.damage;
    p.life[k] = stats.range / def->projectile.speed;
    p.range[k] = stats.range;
    p.splash[k] = stats.splash;
    p.type[k] = static_cast<uint16_t>(b.type);
    // M2.4: where it was launched from (site normal, offsets ignored).
    p.origin[k] = wrapTau(world.siteAngle(s) + world.planetRotation);
  }
}

void projectileMovement(World& world, float dt) {
  if (!world.running()) return;
  ProjectilePool& p = world.projectiles;
  for (uint32_t i = 0; i < p.count; ++i) {
    p.posX[i] += p.velX[i] * dt;
    p.posY[i] += p.velY[i] * dt;
    p.life[i] -= dt;
  }
}

void collisions(World& world, float dt) {
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  ProjectilePool& p = world.projectiles;
  EnemyPool& e = world.enemies;
  const float pad = world.maxEnemyRadius;
  for (uint32_t i = 0; i < p.count;) {
    // The path flown this tick: A -> B.
    const float bx = p.posX[i], by = p.posY[i];
    const float ax = bx - p.velX[i] * dt, ay = by - p.velY[i] * dt;
    uint32_t hit = kInvalidIndex;
    float hitT = 2.0f;
    world.grid.query((ax < bx ? ax : bx) - pad, (ay < by ? ay : by) - pad,
                     (ax > bx ? ax : bx) + pad, (ay > by ? ay : by) + pad, [&](uint32_t k) {
      if (e.flags[k] & kEnemyDead) return;
      const float t = segmentCircle(ax, ay, bx, by, e.posX[k], e.posY[k], db.enemies[e.type[k]].size);
      if (t < 0.0f) return;
      if (t < hitT || (t == hitT && k < hit)) {
        hitT = t;
        hit = k;
      }
    });

    if (hit == kInvalidIndex) {
      if (p.life[i] <= 0.0f) {
        p.remove(i);  // flew its range
      } else {
        ++i;
      }
      continue;
    }

    const BuildingDef& source = db.buildings[p.type[i]];
    const float hx = ax + (bx - ax) * hitT, hy = ay + (by - ay) * hitT;
    // Distance flown at impact as a fraction of range, for the falloff curve.
    const float speed = source.projectile.speed;
    // M2.2: range and splash are the shot's own (the tower's upgrades when it fired).
    const float range = p.range[i];
    const float flown = range - (p.life[i] + (1.0f - hitT) * dt) * speed;
    const float fraction = range > 0.0f ? flown / range : 0.0f;
    const float damage = p.damage[i];
    const float splash = p.splash[i];
    const float origin = p.origin[i];
    // M2.4: weak point or body for enemies that have one; 1x for the rest.
    auto strike = [&](uint32_t k, const EnemyDef& def) {
      const bool weak = (source.precision || !db.rules.weakPointPrecisionOnly) &&
                        isWeakPointHit(def, origin, e.posX[k], e.posY[k]);
      const float dealt = damage * hitMultiplier(db, source, def, fraction, weak);
      if (def.hasWeakPoint) {
        if (weak) {
          ++world.weakPointHits;
          world.emit(EventType::WeakPointHit, e.type[k], p.type[i], e.posX[k], e.posY[k], dealt);
        } else {
          ++world.bodyHits;
        }
      }
      damageEnemy(world, k, dealt, source.projectile.type);
    };
    if (splash > 0.0f) {
      const float reach = splash + pad;
      world.grid.query(hx - reach, hy - reach, hx + reach, hy + reach, [&](uint32_t k) {
        if (e.flags[k] & kEnemyDead) return;
        const EnemyDef& def = db.enemies[e.type[k]];
        const float dx = e.posX[k] - hx, dy = e.posY[k] - hy;
        const float r = splash + def.size;  // the blast touches the enemy's disc
        if (dx * dx + dy * dy > r * r) return;
        strike(k, def);
      });
    } else {
      strike(hit, db.enemies[e.type[hit]]);
    }
    p.remove(i);
  }
}

}  // namespace orbitfall
