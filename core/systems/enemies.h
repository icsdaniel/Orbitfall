#pragma once
// M1.5 enemies: spawning, per-entity state bits and shared helpers.
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// EnemyPool::flags bits.
enum EnemyFlag : uint16_t {
  kEnemyDead = 1u << 0,  // killed or landed this tick; removed by cleanup
  // Bits 1-2: EclipserState for hovering enemies.
  kEnemyStateShift = 1,
  kEnemyStateMask = 3u << 1,
};

// Behaviour state of an EnemyBehavior::Eclipser unit (BRIEF §8.2).
enum class EclipserState : uint16_t { Approach = 0, Hover = 1, Descend = 2 };
inline EclipserState eclipserState(uint16_t flags) {
  return static_cast<EclipserState>((flags & kEnemyStateMask) >> kEnemyStateShift);
}

// Spawner hook: adds one enemy of `enemyType` (index into ContentDB::enemies)
// on lane `lane` of the planet, at the spawn radius on the lane bearing (plus
// seeded jitter when content asks for it), heading for the planet. Returns the
// pool index, or kInvalidIndex when the arguments are bad or the pool is full.
uint32_t spawnEnemy(World& world, int enemyType, int lane);

// Layers left on an enemy of this definition with `hp` on its current layer
// (BRIEF §6 landing: landDamagePerLayer * remaining layers).
int remainingLayers(const EnemyDef& def, float hp);

// Hit points an enemy of this definition starts its current layer with.
inline float layerHp(const EnemyDef& def) {
  return def.popsInto != kNoIndex ? def.hpPerLayer : def.hp;
}

}  // namespace orbitfall
