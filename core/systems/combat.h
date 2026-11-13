#pragma once
// M1.6 combat helpers shared by targeting, firing, collisions and the tests.
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// A tower's world position on the surface and its unit aim axis (BRIEF §6:
// axis angle = slot angle + planet rotation + aimOffset).
struct TowerAim {
  float x = 0.0f, y = 0.0f;    // position T
  float ax = 1.0f, ay = 0.0f;  // aim axis
};
TowerAim towerAim(const World& world, uint32_t slot);

// BRIEF §6 tower arc: |E - T| <= range and the angle between (E - T) and the
// aim axis <= arc.
bool canTarget(const TowerAim& aim, const BuildingDef& def, float ex, float ey);

// Damage multiplier of the falloff curve at `t` = distance / range in [0, 1].
float falloffAt(const BuildingDef& def, float t);

// Final multiplier for a hit: falloff x the enemy's damage-type multiplier,
// never below rules.minDamageMult (no hard counters, floor 0.25).
float damageMultiplier(const ContentDB& db, const BuildingDef& source, const EnemyDef& enemy,
                       float rangeFraction);

// Applies `damage` hit points to enemy `i`, popping layers down the popsInto
// chain (extra damage carries into the next layer). Emits LayerPopped
// (a = old type, b = new type, x/y) per layer and, on the last one,
// EnemyKilled (a = type, b = lane, x/y, value = bounty) plus the bounty.
// The enemy is flagged dead, not removed. Returns true when it died.
bool damageEnemy(World& world, uint32_t i, float damage);

}  // namespace orbitfall
