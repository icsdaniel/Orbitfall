#pragma once
// M2.7 drills, instability and coolers (PLAN §5.4, DECISIONS Q13).
//
// Drills earn stardust at a steady rate whatever the light (paid by `income`).
// Instability is ONE global level (the HUD meter):
//   level = max(0, curve(load) - cooling)
//   load    = sum of the instability weights of the RUNNING drills
//   cooling = sum over coolers of cooling * (litCoolingMult if its slot is lit
//             and unshaded, else 1)
// The curve and every number are data (rules.json "instability", drill and
// cooler JSON). Coolers are a global term, not an aura: where a cooler stands
// matters only through the light (a radiator works best on the night side).
//
// While the level is above the threshold each running drill heats up; at heat
// 1 it overheats and shuts down for overheatSeconds, which takes its weight
// out of the load (the "pause" that relieves the others). Drills are visited
// in site order and the level is recomputed after every shutdown and restart,
// so a tick shuts down only as many drills as it takes to get back under the
// threshold. Nothing is ever damaged: buildings, tiers and planet HP are
// untouched; only the drill's uptime changes.
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// Instability level for a running-drill load and a cooling sum.
float instabilityLevel(const InstabilityDef& def, float load, float cooling);
// The curve alone at `load` (linear between points, extended past the last).
float instabilityCurve(const InstabilityDef& def, float load);

// The system: refreshes World::instability / instabilityLoad / cooling every
// tick (also between waves, so the meter follows building and rotation) and,
// while the wave clock runs, moves heat, overheats and restarts drills
// (DrillOverheated, DrillRestarted). Registered before `income`.
void drills(World& world, float dt);

// Cooling a cooler in this slot gives right now (light-aware). 0 for anything else.
float coolerCooling(const World& world, uint32_t site);

}  // namespace orbitfall
