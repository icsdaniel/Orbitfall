// M1.6: tower targeting and firing, projectile flight, collisions through the
// spatial grid, layer popping, splash and the kill bounty.
#include "systems/combat.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/buildings.h"
#include "systems/enemies.h"
#include "systems/systems.h"

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
  const float angle = world.slotAngle(slot) + world.planetRotation;
  const float r = world.planet->radius;
  aim.x = r * std::cos(angle);
  aim.y = r * std::sin(angle);
  const float axis = angle + world.slots[slot].aimOffset;
  aim.ax = std::cos(axis);
  aim.ay = std::sin(axis);
  return aim;
}

bool canTarget(const TowerAim& aim, const BuildingDef& def, float ex, float ey) {
  const float dx = ex - aim.x, dy = ey - aim.y;
  const float d2 = dx * dx + dy * dy;
  if (d2 > def.range * def.range) return false;
  // angle <= arc  <=>  dot >= |d| * cos(arc)
  return dx * aim.ax + dy * aim.ay >= std::sqrt(d2) * std::cos(def.arc);
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

float damageMultiplier(const ContentDB& db, const BuildingDef& source, const EnemyDef& enemy,
                       float rangeFraction) {
  const float mult = falloffAt(source, clampf(rangeFraction, 0.0f, 1.0f)) *
                     enemy.damageMult[static_cast<int>(source.projectile.type)];
  const float floor = db.rules.minDamageMult > kHardMinDamageMult ? db.rules.minDamageMult
                                                                   : kHardMinDamageMult;
  return mult < floor ? floor : mult;
}

bool damageEnemy(World& world, uint32_t i, float damage) {
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  if (i >= e.count || (e.flags[i] & kEnemyDead) || !(damage > 0.0f)) return false;
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
    e.traits[i] = next.traits;
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
  const uint32_t n = world.slotCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.slots[s];
    b.flags &= static_cast<uint8_t>(~kSlotHasTarget);
    world.slotTargets[s] = kInvalidIndex;
    const BuildingDef* def = world.running() ? towerDef(world, b) : nullptr;
    if (def == nullptr) continue;

    const TowerAim aim = towerAim(world, s);
    const TargetingMode mode = static_cast<TargetingMode>(b.mode);
    uint32_t best = kInvalidIndex;
    float bestKey = 0.0f, bestTie = 0.0f;
    world.grid.query(aim.x - def->range, aim.y - def->range, aim.x + def->range,
                     aim.y + def->range, [&](uint32_t i) {
      if (e.flags[i] & kEnemyDead) return;
      const float ex = e.posX[i], ey = e.posY[i];
      if (!canTarget(aim, *def, ex, ey)) return;
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
  const uint32_t n = world.slotCount();
  for (uint32_t s = 0; s < n; ++s) {
    BuildingSlot& b = world.slots[s];
    const BuildingDef* def = towerDef(world, b);
    if (def == nullptr) continue;
    b.cooldown -= dt;
    const uint32_t target = world.slotTargets[s];
    if (target == kInvalidIndex) {
      if (b.cooldown < 0.0f) b.cooldown = 0.0f;  // ready, waiting
      continue;
    }
    if (b.cooldown > 0.0f || !(def->fireRate > 0.0f) || !(def->projectile.speed > 0.0f)) continue;
    b.cooldown += 1.0f / def->fireRate;  // keeps the remainder, so the average rate is exact
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
    p.damage[k] = def->projectile.damage;
    p.life[k] = def->range / def->projectile.speed;
    p.type[k] = static_cast<uint16_t>(b.type);
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
    const float flown = source.range - (p.life[i] + (1.0f - hitT) * dt) * speed;
    const float fraction = source.range > 0.0f ? flown / source.range : 0.0f;
    const float damage = p.damage[i];
    const float splash = source.projectile.splash;
    if (splash > 0.0f) {
      const float reach = splash + pad;
      world.grid.query(hx - reach, hy - reach, hx + reach, hy + reach, [&](uint32_t k) {
        if (e.flags[k] & kEnemyDead) return;
        const EnemyDef& def = db.enemies[e.type[k]];
        const float dx = e.posX[k] - hx, dy = e.posY[k] - hy;
        const float r = splash + def.size;  // the blast touches the enemy's disc
        if (dx * dx + dy * dy > r * r) return;
        damageEnemy(world, k, damage * damageMultiplier(db, source, def, fraction));
      });
    } else {
      damageEnemy(world, hit, damage * damageMultiplier(db, source, db.enemies[e.type[hit]], fraction));
    }
    p.remove(i);
  }
}

}  // namespace orbitfall
