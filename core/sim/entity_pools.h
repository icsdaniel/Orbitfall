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
  F(float, shield)    /* M2.3: shield points left */           \
  F(float, slow)      /* M2.6: speed fraction taken off */     \
  F(float, slowTime)  /* M2.6: seconds the slow still lasts */ \
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
  F(float, range)    /* M2.2: the firing tower's range */      \
  F(float, splash)   /* M2.2: blast radius, 0 = none */        \
  F(float, slow)     /* M2.6: slow applied on hit */           \
  F(float, shieldMult) /* M2.6: x damage dealt to shields */   \
  F(uint32_t, lastHit) /* M2.6: enemy pierced last, skip it */ \
  F(uint16_t, pierce) /* M2.6: enemies it may still pass */    \
  F(uint16_t, type)  /* projectile definition index */         \
  F(uint16_t, flags) /* per-entity state bits */
ORBITFALL_DECLARE_POOL(ProjectilePool, ORBITFALL_PROJECTILE_FIELDS)

// M2.10: falling objects (meteors, bombs), in world space. They fall straight
// down a fixed world bearing, so `dist` (from the planet centre) and `bearing`
// say everything; pos/vel are kept for rendering.
#define ORBITFALL_FALLING_FIELDS(F)                              \
  F(float, posX)                                                 \
  F(float, posY)                                                 \
  F(float, velX)                                                 \
  F(float, velY)                                                 \
  F(float, bearing) /* rad, world angle it comes down on */      \
  F(float, dist)    /* distance from the planet centre */        \
  F(uint16_t, type) /* falling definition index */               \
  F(uint16_t, flags) /* FallingFlag bits */
ORBITFALL_DECLARE_POOL(FallingPool, ORBITFALL_FALLING_FIELDS)

}  // namespace orbitfall
