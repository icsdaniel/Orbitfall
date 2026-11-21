// M2.2: upgrade paths (see upgrades.h).
#include "systems/upgrades.h"

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/buildings.h"

namespace orbitfall {
namespace {

float* field(BuildingStats& s, UpgradeStat stat) {
  switch (stat) {
    case UpgradeStat::Damage: return &s.damage;
    case UpgradeStat::FireRate: return &s.fireRate;
    case UpgradeStat::Range: return &s.range;
    case UpgradeStat::Arc: return &s.arc;
    case UpgradeStat::Splash: return &s.splash;
    case UpgradeStat::Income: return &s.income;
    case UpgradeStat::Instability: return &s.instability;
    case UpgradeStat::Cooling: return &s.cooling;
    case UpgradeStat::Count: break;
  }
  return nullptr;
}

int pathCount(const BuildingDef& def) {
  const int n = static_cast<int>(def.paths.size());
  return n < kUpgradePaths ? n : kUpgradePaths;
}

int tierCount(const UpgradePathDef& path) {
  const int n = static_cast<int>(path.tiers.size());
  return n < kMaxUpgradeTiers ? n : kMaxUpgradeTiers;
}

// Bought tiers of path `p`, never more than the path declares.
int boughtTiers(const BuildingDef& def, const uint8_t tiers[kUpgradePaths], int p) {
  const int have = tierCount(def.paths[static_cast<size_t>(p)]);
  return tiers[p] < have ? tiers[p] : have;
}

}  // namespace

float BuildingStats::get(UpgradeStat s) const {
  BuildingStats copy = *this;
  const float* f = field(copy, s);
  return f ? *f : 0.0f;
}

BuildingStats baseStats(const BuildingDef& def) {
  BuildingStats s;
  s.damage = def.projectile.damage;
  s.fireRate = def.fireRate;
  s.range = def.range;
  s.arc = def.arc;
  s.splash = def.projectile.splash;
  s.income = def.kind == BuildingKind::Drill ? def.drillRate : def.panelRate;  // M2.7
  s.instability = def.kind == BuildingKind::Drill ? def.instability : 0.0f;
  s.cooling = def.kind == BuildingKind::Cooler ? def.cooling : 0.0f;
  return s;
}

BuildingStats effectiveStats(const BuildingDef& def, const uint8_t tiers[kUpgradePaths]) {
  BuildingStats s = baseStats(def);
  if ((tiers[0] | tiers[1]) == 0 || def.paths.empty()) return s;
  float mul[kUpgradeStatCount];
  for (float& m : mul) m = 1.0f;
  const int paths = pathCount(def);
  for (int p = 0; p < paths; ++p) {
    const UpgradePathDef& path = def.paths[static_cast<size_t>(p)];
    const int bought = boughtTiers(def, tiers, p);
    for (int t = 0; t < bought; ++t) {
      for (const StatModifier& m : path.tiers[static_cast<size_t>(t)].mods) {
        if (m.op == ModOp::Mul) {
          mul[static_cast<int>(m.stat)] *= m.value;
        } else if (float* f = field(s, m.stat)) {
          *f += m.value;
        }
      }
    }
  }
  for (int i = 0; i < kUpgradeStatCount; ++i) {
    float* f = field(s, static_cast<UpgradeStat>(i));
    *f *= mul[i];
    if (*f < 0.0f) *f = 0.0f;
  }
  if (s.arc > kPi) s.arc = kPi;
  return s;
}

int upgradeTierCap(const RulesDef& rules, const BuildingDef& def,
                   const uint8_t tiers[kUpgradePaths], int path) {
  const int paths = pathCount(def);
  if (path < 0 || path >= paths) return 0;
  int cap = tierCount(def.paths[static_cast<size_t>(path)]);
  const int exclusive = rules.upgradeExclusiveTier < 0 ? 0 : rules.upgradeExclusiveTier;
  for (int p = 0; p < paths; ++p) {
    if (p != path && tiers[p] > exclusive && cap > exclusive) cap = exclusive;
  }
  return cap;
}

float upgradeSpent(const BuildingDef& def, const uint8_t tiers[kUpgradePaths]) {
  float spent = 0.0f;
  const int paths = pathCount(def);
  for (int p = 0; p < paths; ++p) {
    const UpgradePathDef& path = def.paths[static_cast<size_t>(p)];
    const int bought = boughtTiers(def, tiers, p);
    for (int t = 0; t < bought; ++t) spent += path.tiers[static_cast<size_t>(t)].cost;
  }
  return spent;
}

UpgradePreview upgradePreview(const RulesDef& rules, const BuildingDef& def,
                              const uint8_t tiers[kUpgradePaths], int path, float stardust) {
  UpgradePreview out;
  out.before = effectiveStats(def, tiers);
  out.after = out.before;
  if (path < 0 || path >= pathCount(def)) return out;
  const UpgradePathDef& pd = def.paths[static_cast<size_t>(path)];
  out.tier = tiers[path];
  out.maxTier = tierCount(pd);
  out.cap = upgradeTierCap(rules, def, tiers, path);
  if (out.tier >= out.maxTier) return out;  // top tier: nothing to preview
  out.hasNext = true;
  out.cost = pd.tiers[static_cast<size_t>(out.tier)].cost;
  uint8_t next[kUpgradePaths] = {tiers[0], tiers[1]};
  next[path] = static_cast<uint8_t>(out.tier + 1);
  out.after = effectiveStats(def, next);
  if (out.tier >= out.cap) return out;  // capped by the other path
  out.reason = stardust < out.cost ? RejectReason::Unaffordable : RejectReason::None;
  return out;
}

bool upgradeBuilding(World& world, int32_t slot, int32_t path) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const float type = static_cast<float>(static_cast<int>(CommandType::UpgradeBuilding));
  RejectReason reason = RejectReason::None;
  float cost = 0.0f;
  // M2.1: `slot` is a site (either ring); any slot of a footprint means its anchor.
  const int16_t anchor = slot >= 0 && slot < static_cast<int32_t>(world.siteCount())
                             ? world.site(static_cast<uint32_t>(slot)).anchor
                             : int16_t(-1);
  if (slot < 0 || slot >= static_cast<int32_t>(world.siteCount())) {
    reason = RejectReason::InvalidSlot;
  } else if (anchor < 0 || world.site(static_cast<uint32_t>(anchor)).type < 0) {
    reason = RejectReason::EmptySlot;
  } else {
    slot = anchor;
    BuildingSlot& b = world.site(static_cast<uint32_t>(anchor));
    const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
    const UpgradePreview pv = upgradePreview(world.content->rules, def, b.tiers, path, world.stardust);
    if (pv.reason == RejectReason::None) {
      b.tiers[path] = static_cast<uint8_t>(pv.tier + 1);
      addStardust(world, -pv.cost);
      world.emit(EventType::BuildingUpgraded, slot, path, static_cast<float>(pv.tier + 1), 0.0f, pv.cost);
      return true;
    }
    reason = pv.reason;
    if (reason == RejectReason::Unaffordable) cost = pv.cost;
  }
  world.emit(EventType::CommandRejected, slot, static_cast<int32_t>(reason), type, 0.0f, cost);
  return false;
}

BuildingStats slotStats(const BuildingDef& def, const BuildingSlot& b) {
  BuildingStats s = effectiveStats(def, b.tiers);
  s.range *= b.rangeMult;
  s.fireRate *= b.fireRateMult;
  s.damage *= b.damageMult;
  s.income *= b.incomeMult;
  return s;
}

}  // namespace orbitfall
