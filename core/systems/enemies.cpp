// M1.5: enemy spawning, movement toward the planet, landing and cleanup.
#include "systems/enemies.h"

#include <cmath>

#include "sim/world.h"
#include "systems/systems.h"

namespace orbitfall {

int remainingLayers(const EnemyDef& def, float hp) {
  if (def.popsInto != kNoIndex) return def.layers;  // one layer per link of the chain
  if (def.layers <= 1 || !(def.hpPerLayer > 0.0f)) return 1;
  const int layers = static_cast<int>(std::ceil(hp / def.hpPerLayer));
  return layers < 1 ? 1 : (layers > def.layers ? def.layers : layers);
}

uint32_t spawnEnemy(World& world, int enemyType, int lane) {
  if (world.content == nullptr || world.planet == nullptr) return kInvalidIndex;
  const ContentDB& db = *world.content;
  if (enemyType < 0 || enemyType >= static_cast<int>(db.enemies.size())) return kInvalidIndex;
  if (lane < 0 || lane >= static_cast<int>(world.planet->lanes.size())) return kInvalidIndex;
  const EnemyDef& def = db.enemies[static_cast<size_t>(enemyType)];

  const float radius = def.hasHover ? def.hover.spawnRadius : db.rules.spawnRadius;
  const float jitter = def.hasHover ? def.hover.spawnJitter : db.rules.spawnJitter;
  float bearing = world.planet->lanes[static_cast<size_t>(lane)].bearing;
  if (jitter > 0.0f) bearing += world.rng.range(-jitter, jitter);

  const uint32_t i = world.enemies.add();
  if (i == kInvalidIndex) return kInvalidIndex;
  const float dx = std::cos(bearing), dy = std::sin(bearing);
  EnemyPool& e = world.enemies;
  e.posX[i] = dx * radius;
  e.posY[i] = dy * radius;
  e.velX[i] = -dx * def.speed;
  e.velY[i] = -dy * def.speed;
  e.hp[i] = layerHp(def);
  e.traits[i] = def.traits;
  e.type[i] = static_cast<uint16_t>(enemyType);
  e.lane[i] = static_cast<uint16_t>(lane);
  ++world.enemiesSpawned;
  return i;
}

namespace {

// One tick of an Eclipser: fly to the hover point on the sun line over the
// most lit panels, keep tracking it while the hover timer runs, then descend
// straight down. Velocity is kept up to date for shot leading and rendering.
void moveEclipser(World& world, uint32_t i, const EnemyDef& def, float dt) {
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
      e.timer[i] -= dt;
      if (e.timer[i] <= 0.0f) state = EclipserState::Descend;
    }
  }
  e.velX[i] = (nx - x) / dt;
  e.velY[i] = (ny - y) / dt;
  e.posX[i] = nx;
  e.posY[i] = ny;
  e.flags[i] = static_cast<uint16_t>((e.flags[i] & ~kEnemyStateMask) |
                                     (static_cast<uint16_t>(state) << kEnemyStateShift));
}

}  // namespace

void enemyMovement(World& world, float dt) {
  if (!world.running() || !(dt > 0.0f)) return;
  const ContentDB& db = *world.content;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count; ++i) {
    const EnemyDef& def = db.enemies[e.type[i]];
    if (def.behavior == EnemyBehavior::Eclipser && def.hasHover) {
      moveEclipser(world, i, def, dt);
    } else {
      e.posX[i] += e.velX[i] * dt;
      e.posY[i] += e.velY[i] * dt;
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
