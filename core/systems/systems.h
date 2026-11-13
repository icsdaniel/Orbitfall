#pragma once
// The game's systems, each `void name(World&, float dt)`, registered in tick
// order in core/sim/pipeline.cpp. Every system is a no-op on a world without
// content, so the bare M0 sim keeps working.

namespace orbitfall {

struct World;

// M1.1: turns the planet from world.rotateInput using the PlanetDef numbers.
void planetRotation(World& world, float dt);

// M1.7: advances the sun along its SunPath (wave time only), sets beamStrength
// and the lit flag of every slot. M1.8: merges the shadow casters' intervals
// into world.shade and sets the shaded flags; emits ShadowChanged (a = shaded
// slots, b = merged intervals) when the set of shaded slots changes.
void sunlight(World& world, float dt);
// M1.7: lit, unshaded panels earn panelRate * beamStrength per second.
// Sets world.incomePerSecond; emits StardustChanged per whole unit earned.
void income(World& world, float dt);
// M1.9: releases the current wave's spawns, spread over its spawnSeconds.
void spawners(World& world, float dt);
// M1.9: ends a wave once everything has spawned and nothing is left
// (WaveCleared, then Intermission or Victory); runs the optional auto-start.
void waveFlow(World& world, float dt);
// M1.5: moves enemies along their velocity. M1.8: Eclipsers fly to their hover
// point on the sun line, track the sun, hover, then descend.
void enemyMovement(World& world, float dt);
// M1.5: re-sorts the enemies into world.grid (counting sort).
void spatialRebuild(World& world, float dt);
// M1.6: picks a target per tower (axis incl. aimOffset, arc, range, mode) into
// world.slotTargets and sets kSlotHasTarget.
void towerTargeting(World& world, float dt);
// M1.6: towers with a target and a ready cooldown fire a led shot.
void towerFire(World& world, float dt);
// M1.6: moves projectiles and ages them.
void projectileMovement(World& world, float dt);
// M1.6: swept projectile-vs-enemy test through the grid; applies damage
// (falloff, type multiplier, floor), splash and layer popping; removes spent
// and expired projectiles.
void collisions(World& world, float dt);
// M1.5: enemies that reached the surface are removed and subtract planet HP
// (landDamagePerLayer * remaining layers, or the flat landDamage). Emits
// PlanetHit (a = enemy type, b = lane, x/y, value = damage) and, at HP 0,
// GameOver (a = wave index); the phase becomes Lost.
void landing(World& world, float dt);
// Removes entities flagged dead during the tick (swap-remove).
void cleanup(World& world, float dt);

}  // namespace orbitfall
