#include "systems/buildings.h"

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/upgrades.h"

namespace orbitfall {
namespace {

bool reject(World& world, CommandType type, int32_t slot, RejectReason reason, float cost = 0.0f) {
  world.emit(EventType::CommandRejected, slot, static_cast<int32_t>(reason),
             static_cast<float>(static_cast<int>(type)), 0.0f, cost);
  return false;
}

bool isFinite(float v) { return v - v == 0.0f; }

float clampAim(float offset) {
  const float limit = kMaxAimOffsetDeg * kPi / 180.0f;
  return isFinite(offset) ? clampf(offset, -limit, limit) : 0.0f;
}

// Shared checks for commands that act on an existing building. `slot` may be
// any slot the building covers; it is replaced by the building's anchor site.
// Returns the definition or nullptr after emitting the rejection.
const BuildingDef* occupied(World& world, CommandType type, int32_t& slot) {
  if (slot < 0 || slot >= static_cast<int32_t>(world.siteCount())) {
    reject(world, type, slot, RejectReason::InvalidSlot);
    return nullptr;
  }
  const int16_t anchor = world.site(static_cast<uint32_t>(slot)).anchor;
  if (anchor < 0) {
    reject(world, type, slot, RejectReason::EmptySlot);
    return nullptr;
  }
  slot = anchor;
  return &world.content->buildings[static_cast<size_t>(world.site(static_cast<uint32_t>(anchor)).type)];
}

// Ring distance in slots between two footprints (0 = they share a column).
int footprintGap(int n, int a, int wa, int b, int wb) {
  int best = n;
  for (int i = 0; i < wa; ++i) {
    for (int j = 0; j < wb; ++j) {
      int d = (a + i - b - j) % n;
      if (d < 0) d += n;
      if (n - d < d) d = n - d;
      if (d < best) best = d;
    }
  }
  return best;
}

float& statOf(BuildingSlot& b, AuraStat stat) {
  switch (stat) {
    case AuraStat::FireRate: return b.fireRateMult;
    case AuraStat::Damage: return b.damageMult;
    case AuraStat::Income: return b.incomeMult;
    case AuraStat::Detect: return b.detect;                  // M2.6
    case AuraStat::ShieldDamage: return b.shieldDamageMult;  // M2.6
    case AuraStat::Range:
    case AuraStat::Count: break;
  }
  return b.rangeMult;
}

}  // namespace

void addStardust(World& world, float delta) {
  world.stardust += delta;
  world.emit(EventType::StardustChanged, 0, 0, delta, 0.0f, world.stardust);
}

void recomputeLayout(World& world) {
  world.auraLinks.clear();
  if (world.content == nullptr || world.planet == nullptr) return;
  const ContentDB& db = *world.content;
  const uint32_t sites = world.siteCount();
  const int n = static_cast<int>(world.slotCount());
  // Pass 1: the multipliers hold the sum of the aura amounts.
  for (uint32_t s = 0; s < sites; ++s) {
    BuildingSlot& b = world.site(s);
    b.rangeMult = b.fireRateMult = b.damageMult = b.incomeMult = 0.0f;
    b.detect = b.shieldDamageMult = 0.0f;  // M2.6
  }
  for (uint32_t s = 0; s < sites; ++s) {
    const BuildingSlot& src = world.site(s);
    if (src.type < 0) continue;
    const BuildingDef& def = db.buildings[static_cast<size_t>(src.type)];
    // M2.6: upgrades scale the amounts and widen the reach of every aura.
    const BuildingStats up = def.auras.empty() ? BuildingStats() : effectiveStats(def, src.tiers);
    for (const AuraDef& aura : def.auras) {
      const float amount = aura.amount * up.auraAmount;
      const int radius = aura.radius + static_cast<int>(up.auraRadius + 0.5f);
      for (uint32_t t = 0; t < sites; ++t) {
        BuildingSlot& dst = world.site(t);
        if (t == s || dst.type < 0) continue;
        const BuildingDef& target = db.buildings[static_cast<size_t>(dst.type)];
        if ((aura.kinds & (1u << static_cast<uint32_t>(target.kind))) == 0) continue;
        if (footprintGap(n, static_cast<int>(s) % n, src.width, static_cast<int>(t) % n, dst.width) >
            radius) {
          continue;
        }
        statOf(dst, aura.stat) += amount;
        AuraLink link;
        link.source = static_cast<uint16_t>(s);
        link.target = static_cast<uint16_t>(t);
        link.stat = static_cast<uint8_t>(aura.stat);
        link.amount = amount;
        world.auraLinks.push_back(link);
      }
    }
  }
  // Pass 2: sums become multipliers; the elevated ring adds its range bonus.
  const auto finish = [](float sum) {
    const float m = 1.0f + sum;
    return m > kMinAuraMult ? m : kMinAuraMult;
  };
  for (uint32_t s = 0; s < sites; ++s) {
    BuildingSlot& b = world.site(s);
    b.rangeMult = finish(b.rangeMult);
    b.fireRateMult = finish(b.fireRateMult);
    b.damageMult = finish(b.damageMult);
    b.incomeMult = finish(b.incomeMult);
    b.shieldDamageMult = finish(b.shieldDamageMult);       // M2.6
    b.detect = clampf(b.detect, 0.0f, 1.0f);               // M2.6: a fraction
    if (world.siteElevated(s)) b.rangeMult *= 1.0f + db.rules.elevatedRangeBonus;
  }
}

bool placeBuilding(World& world, int32_t slot, int32_t building, float aimOffset) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const CommandType type = CommandType::PlaceBuilding;
  if (slot < 0 || slot >= static_cast<int32_t>(world.siteCount())) {
    return reject(world, type, slot, RejectReason::InvalidSlot);
  }
  if (building < 0 || building >= static_cast<int32_t>(world.content->buildings.size())) {
    return reject(world, type, slot, RejectReason::InvalidBuilding);
  }
  const BuildingDef& def = world.content->buildings[static_cast<size_t>(building)];
  const uint32_t n = world.slotCount();
  const uint32_t site = static_cast<uint32_t>(slot);
  const bool elevated = world.siteElevated(site);
  const uint32_t base = elevated ? n : 0u;  // first site of this ring
  const uint32_t first = site - base;
  const uint32_t width = def.footprint > 1 ? static_cast<uint32_t>(def.footprint) : 1u;
  if (width > n) return reject(world, type, slot, RejectReason::InvalidSlot);
  // Every covered slot must be free...
  for (uint32_t k = 0; k < width; ++k) {
    if (world.site(base + (first + k) % n).anchor >= 0) {
      return reject(world, type, slot, RejectReason::Occupied);
    }
  }
  if (def.layer != BuildingLayer::Any &&
      (def.layer == BuildingLayer::Elevated) != elevated) {
    return reject(world, type, slot, RejectReason::WrongLayer);
  }
  // ...and, on the elevated ring, stand on a building.
  if (elevated) {
    for (uint32_t k = 0; k < width; ++k) {
      if (world.slots[(first + k) % n].anchor < 0) {
        return reject(world, type, slot, RejectReason::NoFoundation);
      }
    }
  }
  if (world.stardust < def.cost) return reject(world, type, slot, RejectReason::Unaffordable, def.cost);

  BuildingSlot& b = world.site(site);
  b = BuildingSlot();
  b.type = static_cast<int16_t>(building);
  b.width = static_cast<uint8_t>(width);
  b.mode = def.targeting.empty() ? uint8_t(0) : static_cast<uint8_t>(def.targeting.front());
  b.aimOffset = def.aimMount ? clampAim(def.aimOffset + aimOffset) : 0.0f;
  for (uint32_t k = 0; k < width; ++k) {
    world.site(base + (first + k) % n).anchor = static_cast<int16_t>(site);
  }
  recomputeLayout(world);
  if (def.kind == BuildingKind::Shield) b.energy = def.shield.energy;  // M2.10: placed full and online
  addStardust(world, -def.cost);
  world.emit(EventType::BuildingPlaced, slot, building, 0.0f, 0.0f, def.cost);
  return true;
}

bool sellBuilding(World& world, int32_t slot) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const int32_t asked = slot;
  const BuildingDef* def = occupied(world, CommandType::SellBuilding, slot);
  if (def == nullptr) return false;
  const uint32_t n = world.slotCount();
  const uint32_t site = static_cast<uint32_t>(slot);
  const uint32_t base = world.siteElevated(site) ? n : 0u;
  const uint32_t first = site - base;
  const uint32_t width = world.site(site).width;
  // A foundation stays while anything stands on it.
  if (base == 0 && !world.elevated.empty()) {
    for (uint32_t k = 0; k < width; ++k) {
      if (world.elevated[(first + k) % n].anchor >= 0) {
        return reject(world, CommandType::SellBuilding, asked, RejectReason::SupportsElevated);
      }
    }
  }
  const int32_t building = world.site(site).type;
  // M2.2: the refund also covers what was spent on upgrades.
  const float refund =
      (def->cost + upgradeSpent(*def, world.site(site).tiers)) * world.planet->sellRefund;
  for (uint32_t k = 0; k < width; ++k) world.site(base + (first + k) % n) = BuildingSlot();
  recomputeLayout(world);
  addStardust(world, refund);
  world.emit(EventType::BuildingSold, slot, building, 0.0f, 0.0f, refund);
  return true;
}

bool setAim(World& world, int32_t slot, float aimOffset) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const BuildingDef* def = occupied(world, CommandType::SetAim, slot);
  if (def == nullptr) return false;
  if (!def->aimMount) return reject(world, CommandType::SetAim, slot, RejectReason::NotAllowed);
  world.site(static_cast<uint32_t>(slot)).aimOffset = clampAim(aimOffset);
  return true;
}

bool setTargeting(World& world, int32_t slot, uint8_t mode) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const BuildingDef* def = occupied(world, CommandType::SetTargeting, slot);
  if (def == nullptr) return false;
  for (const TargetingMode m : def->targeting) {
    if (static_cast<uint8_t>(m) == mode) {
      world.site(static_cast<uint32_t>(slot)).mode = mode;
      return true;
    }
  }
  return reject(world, CommandType::SetTargeting, slot, RejectReason::NotAllowed);
}

}  // namespace orbitfall
