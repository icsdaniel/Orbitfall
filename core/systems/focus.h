#pragma once
// M2.11 bent aim and focus fire (BRIEF §4.4, §6; DESIGN Q33, Q34).
//
// Aim-mounted towers fire along slot normal + aimOffset. Where the axes of two
// or more of them cross within their ranges a focus point forms, in planet-local
// coordinates (so it turns with the planet). solveFocus recomputes the points
// whenever the layout or an aim changes, never per tick. Hits inside a zone from
// a tower that feeds it deal the focus bonus (rules.focus); two or more Sun
// Lens beams feeding one zone merge (systems/towers.cpp).
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// Recomputes World::focus from the standing aim-mounted towers: for every pair
// solve T_A + a d_A = T_B + b d_B; a crossing counts iff a, b > 0, both within
// range and at least rules.focus.minAltitude above the surface. Crossings
// closer than rules.focus.radius to an existing point merge into it (running
// mean of the crossings), and the point gains both towers as feeders.
// Deterministic (site order); allocation-free once World::focus is reserved.
void solveFocus(World& world);

// Focus multiplier for `feeders` towers feeding one zone:
// 1 + min(bonusPerExtra x (feeders - 1), cap); 1 for fewer than two.
float focusMultiplier(const FocusRulesDef& rules, int feeders);

// Most feeders of any zone that `site` feeds and that holds the world point
// (wx, wy) right now; 0 when there is none.
int focusFeedersAt(const World& world, uint32_t site, float wx, float wy);

// Damage multiplier for a hit at world (wx, wy) from `site` (1 = no bonus).
float focusBonusAt(const World& world, uint32_t site, float wx, float wy);

// Zones `site` feeds, as world centres and squared radii at the current
// rotation, at most kMaxZonesPerTower (towerTargeting's zone preference).
inline constexpr int kMaxZonesPerTower = 8;
int feederZones(const World& world, uint32_t site, float* x, float* y, float* r2);

// World position of focus point `i` at the current planet rotation.
void focusWorldPos(const World& world, uint32_t i, float& wx, float& wy);

// M2.11 re-aim: the offset an aim request snaps to (nearest of
// rules.aim.offsets, clamped to the bent-aim limit), and the price of a SetAim.
float snapAimOffset(const RulesDef& rules, float offset);
float reaimCost(const RulesDef& rules, const BuildingDef& def);

}  // namespace orbitfall
