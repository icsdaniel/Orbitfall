#pragma once
// The game's systems, each `void name(World&, float dt)`, registered in tick
// order in core/sim/pipeline.cpp. Every system is a no-op on a world without
// content, so the bare M0 sim keeps working.

namespace orbitfall {

struct World;

// M1.1: turns the planet from world.rotateInput using the PlanetDef numbers.
void planetRotation(World& world, float dt);

// M1.5: moves enemies along their velocity.
void enemyMovement(World& world, float dt);
// M1.5: re-sorts the enemies into world.grid (counting sort).
void spatialRebuild(World& world, float dt);
// M1.5: enemies that reached the surface are removed and subtract planet HP
// (landDamagePerLayer * remaining layers, or the flat landDamage). Emits
// PlanetHit (a = enemy type, b = lane, x/y, value = damage) and, at HP 0,
// GameOver (a = wave index); the phase becomes Lost.
void landing(World& world, float dt);
// Removes entities flagged dead during the tick (swap-remove).
void cleanup(World& world, float dt);

}  // namespace orbitfall
