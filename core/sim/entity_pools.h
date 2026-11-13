#pragma once
// The entity pools that live in World. Field meanings are engine-level only:
// `type` is an index into loaded content (never a content id).
#include <cstdint>

#include "pools/pool.h"

namespace orbitfall {

// Enemies, in world space.
#define ORBITFALL_ENEMY_FIELDS(F)                              \
  F(float, posX)                                               \
  F(float, posY)                                               \
  F(float, velX)                                               \
  F(float, velY)                                               \
  F(float, hp)        /* hit points left on this layer */      \
  F(float, timer)     /* behaviour timer, seconds */           \
  F(uint32_t, traits) /* trait bits */                         \
  F(uint16_t, type)   /* enemy definition index */             \
  F(uint16_t, flags)  /* EnemyFlag bits and behaviour state */ \
  F(uint16_t, lane)   /* planet lane it spawned on */
ORBITFALL_DECLARE_POOL(EnemyPool, ORBITFALL_ENEMY_FIELDS)

// Projectiles, in world space.
#define ORBITFALL_PROJECTILE_FIELDS(F)                         \
  F(float, posX)                                               \
  F(float, posY)                                               \
  F(float, velX)                                               \
  F(float, velY)                                               \
  F(float, damage)                                             \
  F(float, life)     /* seconds left before it expires */      \
  F(uint16_t, type)  /* projectile definition index */         \
  F(uint16_t, flags) /* per-entity state bits */
ORBITFALL_DECLARE_POOL(ProjectilePool, ORBITFALL_PROJECTILE_FIELDS)

}  // namespace orbitfall
