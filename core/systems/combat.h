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
TowerAim towerAim(const World& world, uint32_t slot);  // slot = site index

// BRIEF §6 tower arc: |E - T| <= range and the angle between (E - T) and the
// aim axis <= arc. `rangeMult` is the slot's range multiplier (M2.1: elevated
// bonus and adjacency auras).
bool canTarget(const TowerAim& aim, const BuildingDef& def, float ex, float ey,
               float rangeMult = 1.0f);
// M2.2: the same test with explicit (upgraded) range and arc half-angle.
bool canTarget(const TowerAim& aim, float range, float arc, float ex, float ey);

// Damage multiplier of the falloff curve at `t` = distance / range in [0, 1].
float falloffAt(const BuildingDef& def, float t);

// M2.3: the damage floor in force, rules.minDamageMult but never under 0.25.
float damageFloor(const ContentDB& db);

// M2.3 soft-counter matrix: the enemy's own damageMult for `type` times the
// row of every trait it carries (content/damage.json), floored.
float typeMultiplier(const ContentDB& db, const EnemyDef& enemy, DamageType type);

// M2.3: multiplier on damage of `type` dealt to a shield, floored.
float shieldMultiplier(const ContentDB& db, DamageType type);

// Final multiplier for a hit: falloff x typeMultiplier(), never below
// rules.minDamageMult (no hard counters, floor 0.25).
float damageMultiplier(const ContentDB& db, const BuildingDef& source, const EnemyDef& enemy,
                       float rangeFraction);

// Applies `damage` hit points to enemy `i`, popping layers down the popsInto
// chain (extra damage carries into the next layer). Emits LayerPopped
// (a = old type, b = new type, x/y) per layer and, on the last one,
// EnemyKilled (a = type, b = lane, x/y, value = bounty) plus the bounty.
// The enemy is flagged dead, not removed. Returns true when it died.
// M2.3: a shield absorbs first (damage x shieldMultiplier(type)); what is left
// over after it breaks goes on to the hit points, and the entity loses its
// Shielded trait bit so the glow ring disappears.
// M2.6: `shieldBonus` multiplies what reaches a shield (Amplifier aura).
bool damageEnemy(World& world, uint32_t i, float damage, DamageType type = DamageType::Kinetic,
                 float shieldBonus = 1.0f);

}  // namespace orbitfall
