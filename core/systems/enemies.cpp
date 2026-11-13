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

void enemyMovement(World& world, float dt) {
  if (!world.running()) return;
  EnemyPool& e = world.enemies;
  for (uint32_t i = 0; i < e.count; ++i) {
    e.posX[i] += e.velX[i] * dt;
    e.posY[i] += e.velY[i] * dt;
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
