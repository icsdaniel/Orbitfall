// M1.5: enemy spawning, movement toward the planet, landing and cleanup.
#include "systems/enemies.h"

#include <cmath>

#include "sim/world.h"
#include "systems/falling.h"
#include "systems/systems.h"

namespace orbitfall {

int remainingLayers(const EnemyDef& def, float hp) {
  if (def.popsInto != kNoIndex) return def.layers;  // one layer per link of the chain
  if (def.layers <= 1 || !(def.hpPerLayer > 0.0f)) return 1;
  const int layers = static_cast<int>(std::ceil(hp / def.hpPerLayer));
  return layers < 1 ? 1 : (layers > def.layers ? def.layers : layers);
}

namespace {

// Fills enemy `i` of `enemyType` at (x, y), heading for the planet centre.
void initEnemy(World& world, uint32_t i, int enemyType, int lane, float x, float y) {
  const EnemyDef& def = world.content->enemies[static_cast<size_t>(enemyType)];
  EnemyPool& e = world.enemies;
  const float len = std::sqrt(x * x + y * y);
  const float dx = len > 0.0f ? x / len : 0.0f, dy = len > 0.0f ? y / len : 0.0f;
  e.posX[i] = x;
  e.posY[i] = y;
  e.velX[i] = -dx * def.speed;
  e.velY[i] = -dy * def.speed;
  e.hp[i] = layerHp(def);
  e.traits[i] = def.traits;
  e.shield[i] = def.shield;
  e.type[i] = static_cast<uint16_t>(enemyType);
  e.spawnType[i] = static_cast<uint16_t>(enemyType);  // M2.5
  e.lane[i] = static_cast<uint16_t>(lane);
  ++world.enemiesSpawned;
}

}  // namespace

uint32_t spawnEnemy(World& world, int enemyType, int lane) {
  if (world.content == nullptr || world.planet == nullptr) return kInvalidIndex;
  const ContentDB& db = *world.content;
  if (enemyType < 0 || enemyType >= static_cast<int>(db.enemies.size())) return kInvalidIndex;
  if (lane < 0 || lane >= static_cast<int>(world.planet->lanes.size())) return kInvalidIndex;
  const EnemyDef& def = db.enemies[static_cast<size_t>(enemyType)];

  const float radius = def.hasHover ? def.hover.spawnRadius : db.rules.spawnRadius;
  const float jitter = def.hasHover ? def.hover.spawnJitter
                                    : (def.laneJitter >= 0.0f ? def.laneJitter : db.rules.spawnJitter);
  float bearing = world.planet->lanes[static_cast<size_t>(lane)].bearing;
  if (jitter > 0.0f) bearing += world.rng.range(-jitter, jitter);

  const uint32_t i = world.enemies.add();
  if (i == kInvalidIndex) return kInvalidIndex;
  initEnemy(world, i, enemyType, lane, std::cos(bearing) * radius, std::sin(bearing) * radius);
  return i;
}

uint32_t spawnEnemyAt(World& world, int enemyType, int lane, float x, float y) {
  if (world.content == nullptr) return kInvalidIndex;
  if (enemyType < 0 || enemyType >= static_cast<int>(world.content->enemies.size())) return kInvalidIndex;
  const uint32_t i = world.enemies.add();
  if (i == kInvalidIndex) return kInvalidIndex;
  initEnemy(world, i, enemyType, lane, x, y);
  return i;
}

int regrowParent(const ContentDB& db, int top, int current) {
  const int n = static_cast<int>(db.enemies.size());
  int t = top;
  for (int steps = 0; t >= 0 && t < n && steps < n; ++steps) {
    const int next = db.enemies[static_cast<size_t>(t)].popsInto;
    if (next == current) return t;
    t = next;
  }
  return kNoIndex;
}

namespace {

// One tick of an Eclipser: fly to the hover point on the sun line over the
// most lit panels, keep tracking it while the hover timer runs, then descend
// straight down. Velocity is kept up to date for shot leading and rendering.
// M2.6: `dt` is the slowed step (sim dt x speedFactor), `tickDt` the tick's
// own dt (hover timer, velocity).
void moveEclipser(World& world, uint32_t i, const EnemyDef& def, float dt, float tickDt) {
  EnemyPool& e = world.enemies;
  EclipserState state = eclipserState(e.flags[i]);
  float x = e.posX[i], y = e.posY[i];
  float nx = x, ny = y;
  if (state == EclipserState::Descend) {
    const float len = std::sqrt(x * x + y * y);
    if (len > 0.0f) {
      nx = x - x / len * def.hover.descendSpeed * dt;
      ny = y - y / len * def.hover.descendSpeed * dt;
    }
  } else {
    const float sx = std::cos(world.sunBearing), sy = std::sin(world.sunBearing);
    const float offset = eclipserOffset(world, def.size);
    const float hx = sx * def.hover.hoverRadius - sy * offset;
    const float hy = sy * def.hover.hoverRadius + sx * offset;
    const float dx = hx - x, dy = hy - y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float step = def.speed * dt;
    if (dist <= step) {
      nx = hx;
      ny = hy;
      if (state == EclipserState::Approach) {
        state = EclipserState::Hover;
        e.timer[i] = def.hover.hoverSeconds;
      }
    } else {
      nx = x + dx / dist * step;
      ny = y + dy / dist * step;
    }
    if (state == EclipserState::Hover) {
      e.timer[i] -= tickDt;
      if (e.timer[i] <= 0.0f) state = EclipserState::Descend;
    }
  }
  // Unslowed velocity: speedFactor scales it wherever it is used.
  e.velX[i] = dt > 0.0f ? (nx - x) / dt : 0.0f;
  e.velY[i] = dt > 0.0f ? (ny - y) / dt : 0.0f;
  e.posX[i] = nx;
  e.posY[i] = ny;
  e.flags[i] = static_cast<uint16_t>((e.flags[i] & ~kEnemyStateMask) |
                                     (static_cast<uint16_t>(state) << kEnemyStateShift));
}

// M2.10: one tick of a Bomber (BomberState). Bombs fall from where it is.
void moveBomber(World& world, uint32_t i, const EnemyDef& def, float dt) {
  EnemyPool& e = world.enemies;
  BomberState state = bomberState(e.flags[i]);
  int dropped = bombsDropped(e.flags[i]);
  const float x = e.posX[i], y = e.posY[i];
  const float dist = std::sqrt(x * x + y * y);
  float nx = x, ny = y;
  const BomberDef& b = def.bomber;
  if (state == BomberState::Approach) {
    const float step = def.speed * dt;
    if (dist - step <= b.orbitRadius && dist > 0.0f) {
      nx = x / dist * b.orbitRadius;
      ny = y / dist * b.orbitRadius;
      state = b.bombs > dropped ? BomberState::Orbit : BomberState::Dive;
      e.timer[i] = 0.5f * b.dropSeconds;  // first bomb half a period after arriving
    } else if (dist > 0.0f) {
      nx = x - x / dist * step;
      ny = y - y / dist * step;
    }
  } else if (state == BomberState::Orbit) {
    const float angle = std::atan2(y, x) + b.orbitSpeed * dt;
    nx = std::cos(angle) * b.orbitRadius;
    ny = std::sin(angle) * b.orbitRadius;
    e.timer[i] -= dt;
    if (e.timer[i] <= 0.0f) {
      spawnFalling(world, b.bomb, angle, b.orbitRadius);  // a full pool drops the bomb
      ++dropped;
      e.timer[i] += b.dropSeconds;
      if (dropped >= b.bombs) state = BomberState::Dive;
    }
  } else if (dist > 0.0f) {
    const float step = def.speed * dt;
    nx = x - x / dist * step;
    ny = y - y / dist * step;
  }
  e.velX[i] = (nx - x) / dt;
  e.velY[i] = (ny - y) / dt;
  e.posX[i] = nx;
  e.posY[i] = ny;
  e.flags[i] = static_cast<uint16_t>((e.flags[i] & ~(kEnemyStateMask | kEnemyBombMask)) |
                                     (static_cast<uint16_t>(state) << kEnemyStateShift) |
                                     (static_cast<uint16_t>(dropped) << kEnemyBombShift));
}

}  // namespace

void enemyMovement(World& world, float dt) {
  if (!world.running() || !(dt > 0.0f)) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count; ++i) {
    const EnemyDef& def = db.enemies[e.type[i]];
    // M2.6: a slowed enemy covers less ground for as long as the slow lasts.
    const float step = dt * speedFactor(e, i);
    if (e.slowTime[i] > 0.0f) {
      e.slowTime[i] -= dt;
      if (e.slowTime[i] <= 0.0f) {
        e.slowTime[i] = 0.0f;
        e.slow[i] = 0.0f;
      }
    }
    if (def.behavior == EnemyBehavior::Eclipser && def.hasHover) {
      moveEclipser(world, i, def, step, dt);
    } else if (def.behavior == EnemyBehavior::Bomber && def.hasBomber) {
      moveBomber(world, i, def, step);  // M2.6: slowed like the rest
    } else {
      e.posX[i] += e.velX[i] * step;
      e.posY[i] += e.velY[i] * step;
    }
  }
}

void spatialRebuild(World& world, float /*dt*/) {
  if (!world.running()) return;
  world.grid.rebuild(world.enemies.posX, world.enemies.posY, world.enemies.count);
}

void landing(World& world, float /*dt*/) {
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  const float r2 = world.planet->radius * world.planet->radius;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count; ++i) {
    if (e.flags[i] & kEnemyDead) continue;
    if (e.posX[i] * e.posX[i] + e.posY[i] * e.posY[i] > r2) continue;
    const EnemyDef& def = db.enemies[e.type[i]];
    const float damage = def.land_damage(remainingLayers(def, e.hp[i]));
    e.flags[i] |= kEnemyDead;
    world.planetHp -= damage;
    if (world.planetHp < 0.0f) world.planetHp = 0.0f;
    world.emit(EventType::PlanetHit, e.type[i], e.lane[i], e.posX[i], e.posY[i], damage);
    if (world.planetHp <= 0.0f && world.phase != Phase::Lost) {
      world.phase = Phase::Lost;
      world.emit(EventType::GameOver, static_cast<int32_t>(world.waveIndex));
    }
  }
}

// --- M2.5 enemies batch 1 ---

void regrow(World& world, float dt) {
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count; ++i) {
    if (!(e.traits[i] & kTraitRegrow) || (e.flags[i] & kEnemyDead)) continue;
    e.calm[i] += dt;
    const EnemyDef& def = db.enemies[e.type[i]];
    if (!(def.regrowSeconds > 0.0f) || e.calm[i] < def.regrowSeconds) continue;
    e.calm[i] = 0.0f;
    const int up = regrowParent(db, e.spawnType[i], e.type[i]);
    if (up == kNoIndex) continue;  // at the layer it spawned with: nothing to regrow
    const EnemyDef& next = db.enemies[static_cast<size_t>(up)];
    world.emit(EventType::LayerRegrown, e.type[i], up, e.posX[i], e.posY[i]);
    if (def.speed > 0.0f && next.speed != def.speed) {
      const float scale = next.speed / def.speed;
      e.velX[i] *= scale;
      e.velY[i] *= scale;
    }
    e.type[i] = static_cast<uint16_t>(up);
    e.hp[i] = layerHp(next);
    // A broken shield stays broken (as on popping down); other traits follow the layer.
    e.traits[i] = e.shield[i] > 0.0f ? next.traits : next.traits & ~static_cast<uint32_t>(kTraitShielded);
  }
}

void splits(World& world, float /*dt*/) {
  if (!world.running()) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  const uint32_t n = e.count;  // children are appended past n and are not visited
  for (uint32_t i = 0; i < n; ++i) {
    if ((e.flags[i] & (kEnemyDead | kEnemySplit)) != (kEnemyDead | kEnemySplit)) continue;
    e.flags[i] &= static_cast<uint16_t>(~kEnemySplit);
    const EnemyDef& def = db.enemies[e.type[i]];
    if (def.splitInto == kNoIndex || def.splitCount <= 0) continue;
    const float x = e.posX[i], y = e.posY[i];
    const float len = std::sqrt(x * x + y * y);
    // Sideways to the path (tangent to the planet), evenly over +-spread.
    const float tx = len > 0.0f ? -y / len : 1.0f, ty = len > 0.0f ? x / len : 0.0f;
    int spawned = 0;
    for (int k = 0; k < def.splitCount; ++k) {
      const float u = def.splitCount > 1
                          ? 2.0f * static_cast<float>(k) / static_cast<float>(def.splitCount - 1) - 1.0f
                          : 0.0f;
      const float off = u * def.splitSpread;
      if (spawnEnemyAt(world, def.splitInto, e.lane[i], x + tx * off, y + ty * off) != kInvalidIndex)
        ++spawned;  // a full pool drops the child (overflowCount)
    }
    world.emit(EventType::EnemySplit, e.type[i], def.splitInto, x, y, static_cast<float>(spawned));
  }
}

void cleanup(World& world, float /*dt*/) {
  if (world.content == nullptr) return;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count;) {
    if (e.flags[i] & kEnemyDead) {
      e.remove(i);
    } else {
      ++i;
    }
  }
}

}  // namespace orbitfall
