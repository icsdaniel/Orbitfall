#pragma once
// M1.5 enemies: spawning, per-entity state bits and shared helpers.
#include <cstdint>

#include "content/content.h"
#include "sim/entity_pools.h"

namespace orbitfall {

struct World;

// EnemyPool::flags bits.
enum EnemyFlag : uint16_t {
  kEnemyDead = 1u << 0,  // killed or landed this tick; removed by cleanup
  // Bits 1-2: EclipserState for hovering enemies.
  kEnemyStateShift = 1,
  kEnemyStateMask = 3u << 1,
  // M2.10: bits 3-8 count the bombs a Bomber has dropped.
  kEnemyBombShift = 3,
  kEnemyBombMask = 63u << 3,
  // M2.5: killed by damage with a split to do; the splits system spawns the
  // children before cleanup removes it.
  kEnemySplit = 1u << 9,
};

// Behaviour state of an EnemyBehavior::Eclipser unit (BRIEF §8.2).
enum class EclipserState : uint16_t { Approach = 0, Hover = 1, Descend = 2 };
inline EclipserState eclipserState(uint16_t flags) {
  return static_cast<EclipserState>((flags & kEnemyStateMask) >> kEnemyStateShift);
}

// M2.10: behaviour state of an EnemyBehavior::Bomber unit (same bits): fly in
// on its lane to orbitRadius, circle the planet dropping a bomb every
// dropSeconds, then dive at the planet.
enum class BomberState : uint16_t { Approach = 0, Orbit = 1, Dive = 2 };
inline BomberState bomberState(uint16_t flags) {
  return static_cast<BomberState>((flags & kEnemyStateMask) >> kEnemyStateShift);
}
inline int bombsDropped(uint16_t flags) { return (flags & kEnemyBombMask) >> kEnemyBombShift; }

// Spawner hook: adds one enemy of `enemyType` (index into ContentDB::enemies)
// on lane `lane` of the planet, at the spawn radius on the lane bearing (plus
// seeded jitter when content asks for it), heading for the planet. Returns the
// pool index, or kInvalidIndex when the arguments are bad or the pool is full.
uint32_t spawnEnemy(World& world, int enemyType, int lane);
// M2.5: same, at world position (x, y), heading for the planet centre (used
// for Splitter children). Returns kInvalidIndex when the pool is full.
uint32_t spawnEnemyAt(World& world, int enemyType, int lane, float x, float y);

// M2.5 Regrow: the layer above `current` in the popsInto chain that starts at
// `top` (the type the unit spawned as), or kNoIndex when `current` is `top`
// or not in that chain.
int regrowParent(const ContentDB& db, int top, int current);

// Layers left on an enemy of this definition with `hp` on its current layer
// (BRIEF §6 landing: landDamagePerLayer * remaining layers).
int remainingLayers(const EnemyDef& def, float hp);

// M2.6: fraction of its speed a (Cryo) slowed enemy keeps right now.
inline float speedFactor(const EnemyPool& e, uint32_t i) {
  return e.slowTime[i] > 0.0f ? 1.0f - e.slow[i] : 1.0f;
}

// M2.6: slows enemy `i` by `slow` (the stronger slow wins) for `seconds`.
inline void applySlow(EnemyPool& e, uint32_t i, float slow, float seconds) {
  if (!(slow > 0.0f) || !(seconds > 0.0f)) return;
  if (slow > kMaxSlow) slow = kMaxSlow;
  if (e.slowTime[i] <= 0.0f || slow > e.slow[i]) e.slow[i] = slow;
  if (seconds > e.slowTime[i]) e.slowTime[i] = seconds;
}

// Hit points an enemy of this definition starts its current layer with.
inline float layerHp(const EnemyDef& def) {
  return def.popsInto != kNoIndex ? def.hpPerLayer : def.hp;
}

}  // namespace orbitfall
