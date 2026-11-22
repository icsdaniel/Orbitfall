#pragma once
// M2.6 towers batch 1: Sun Lens beam and active abilities (see towers.cpp).
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// Sun Lens: share of its full damage the channel value gives,
// channelFloor + (1 - channelFloor) x channel (PLAN 5.6, DECISIONS Q26).
float channelFactor(const BuildingDef& def, float channel);
// Sun Lens: share of its full damage after `seconds` on one target.
float rampFactor(const BuildingDef& def, float seconds);
// Lit, unshaded fraction of the planet's solar panel capacity (upgrades and
// auras included); 0 without panels. sunlight stores it in World::channel.
float panelChannel(const World& world);

// The ActivateBuilding command: starts the building's active ability (Sun Lens
// Overcharge) on `slot` (any site of its footprint). Emits BuildingActivated
// (a = anchor site, b = building, value = cooldown) or CommandRejected
// (InvalidSlot, EmptySlot, NotAllowed without an active, ActiveNotReady).
bool activateBuilding(World& world, int32_t slot);

}  // namespace orbitfall
