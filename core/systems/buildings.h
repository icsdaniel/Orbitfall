#pragma once
// M1.3 slot ring: placing and selling buildings. Called by applyCommands for
// the PlaceBuilding / SellBuilding / SetAim / SetTargeting commands; each
// returns true when applied and otherwise emits CommandRejected with a
// RejectReason. On a world without content they do nothing.
#include <cstdint>

namespace orbitfall {

struct World;

// Pays the building's cost and puts it on an empty slot. `aimOffset` (rad) is
// added to the definition's default mount offset and clamped to the bent-aim
// limit; it is ignored for buildings without an aim mount.
bool placeBuilding(World& world, int32_t slot, int32_t building, float aimOffset);
// Removes the building and refunds cost * planet sellRefund.
bool sellBuilding(World& world, int32_t slot);
// Sets the mount offset of an aim-mounted building (free in M1; M2.11 prices it).
bool setAim(World& world, int32_t slot, float aimOffset);
// Picks one of the targeting modes the building's definition lists.
bool setTargeting(World& world, int32_t slot, uint8_t mode);

// Adds `delta` stardust and emits StardustChanged (value = new total, x = delta).
void addStardust(World& world, float delta);

}  // namespace orbitfall
