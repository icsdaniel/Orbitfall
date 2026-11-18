#pragma once
// M2.2 upgrade paths: a building may declare up to kUpgradePaths paths of up to
// kMaxUpgradeTiers tiers in content (BuildingDef::paths). A slot remembers the
// tier bought on each path (BuildingSlot::tiers); the stats the systems use are
// derived from the definition and those tiers, never stored:
//   effective = (base + sum of "add" modifiers) * product of "mul" modifiers
// over every bought tier of both paths. Paths are exclusive past
// rules.upgradeExclusiveTier: once one path is above it, the others stop at it.
#include <cstdint>

#include "content/content.h"
#include "sim/events.h"

namespace orbitfall {

struct World;
struct BuildingSlot;

// The numbers upgrades can change, one per UpgradeStat.
struct BuildingStats {
  float damage = 0.0f;
  float fireRate = 0.0f;
  float range = 0.0f;
  float arc = 0.0f;  // rad, half-angle
  float splash = 0.0f;
  float income = 0.0f;  // panelRate

  float get(UpgradeStat s) const;
};

// The definition's own numbers (no upgrades).
BuildingStats baseStats(const BuildingDef& def);
// Stats with `tiers[p]` tiers bought on path p. No allocation; safe every tick.
BuildingStats effectiveStats(const BuildingDef& def, const uint8_t tiers[kUpgradePaths]);

// Highest tier `path` may reach right now given the other path's tier
// (0 when the building has no such path).
int upgradeTierCap(const RulesDef& rules, const BuildingDef& def,
                   const uint8_t tiers[kUpgradePaths], int path);
// Stardust paid for the bought tiers.
float upgradeSpent(const BuildingDef& def, const uint8_t tiers[kUpgradePaths]);

// What buying the next tier of `path` would do. `reason` is None when the
// upgrade is allowed, UpgradeBlocked (no path / top tier / capped by the other
// path) or Unaffordable. `cost` and `after` describe the next tier whenever
// one exists (`hasNext`), also when it is blocked or unaffordable.
struct UpgradePreview {
  RejectReason reason = RejectReason::UpgradeBlocked;
  bool hasNext = false;
  int tier = 0;     // current tier of the path
  int maxTier = 0;  // tiers the path declares
  int cap = 0;      // upgradeTierCap
  float cost = 0.0f;
  BuildingStats before, after;
};
UpgradePreview upgradePreview(const RulesDef& rules, const BuildingDef& def,
                              const uint8_t tiers[kUpgradePaths], int path, float stardust);

// What a standing building uses: effectiveStats times the slot's M2.1
// multipliers (elevated range bonus, adjacency auras). No allocation.
BuildingStats slotStats(const BuildingDef& def, const BuildingSlot& b);

// The UpgradeBuilding command: pays for and applies the next tier of `path` on
// the building in `slot` (a site, M2.1; any slot of a footprint means its anchor). Emits BuildingUpgraded (a = slot, b = path, x = new
// tier, value = cost) or CommandRejected (InvalidSlot, EmptySlot,
// UpgradeBlocked, or Unaffordable with value = cost).
bool upgradeBuilding(World& world, int32_t slot, int32_t path);

}  // namespace orbitfall
