#pragma once
// M1.3 slot ring: placing and selling buildings. Called by applyCommands for
// the PlaceBuilding / SellBuilding / SetAim / SetTargeting commands; each
// returns true when applied and otherwise emits CommandRejected with a
// RejectReason. On a world without content they do nothing.
#include <cstdint>

namespace orbitfall {

struct World;

// M2.1: `slot` is a site index (World): slot + layer * ring slots, so M1 slot
// numbers still mean the ground ring. Sell / aim / targeting accept any slot a
// building covers.
//
// Pays the building's cost and puts it on an empty slot (all the slots of its
// footprint, starting at `slot`, must be free; an elevated slot needs a
// building on every ground slot below: NoFoundation otherwise). `aimOffset` (rad) is
// added to the definition's default mount offset and clamped to the bent-aim
// limit; it is ignored for buildings without an aim mount.
bool placeBuilding(World& world, int32_t slot, int32_t building, float aimOffset);
// Removes the building and refunds (cost + upgrade costs paid) * planet
// sellRefund (M2.2). A ground building that carries an elevated one cannot be
// sold (SupportsElevated).
bool sellBuilding(World& world, int32_t slot);
// Sets the mount offset of an aim-mounted building. M2.11: the offset snaps to
// rules.aim.offsets; a change costs rules.aim.reaimCostFrac of the building's
// cost (Unaffordable otherwise) and, with rules.aim.reaimBetweenWavesOnly, is
// refused while a wave runs (WrongPhase). Emits AimChanged and re-solves the
// focus points; asking for the current offset is a free no-op.
bool setAim(World& world, int32_t slot, float aimOffset);
// Picks one of the targeting modes the building's definition lists.
bool setTargeting(World& world, int32_t slot, uint8_t mode);

// M2.1: recomputes every slot's stat multipliers (elevated range bonus and
// adjacency auras) and World::auraLinks. Called when the layout changes, never
// per tick.
void recomputeLayout(World& world);
// No aura stack can push a stat below this fraction of its definition.
inline constexpr float kMinAuraMult = 0.1f;

// Adds `delta` stardust and emits StardustChanged (value = new total, x = delta).
void addStardust(World& world, float delta);

}  // namespace orbitfall
