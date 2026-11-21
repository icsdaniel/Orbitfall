// M2.7: drills, instability and coolers (see drills.h).
#include "systems/drills.h"

#include "sim/world.h"
#include "systems/upgrades.h"

namespace orbitfall {

float instabilityCurve(const InstabilityDef& def, float load) {
  const std::vector<CurvePoint>& c = def.curve;
  if (c.empty()) return 0.0f;
  if (c.size() == 1 || load <= c.front().x) return c.front().y;
  for (size_t i = 1; i < c.size(); ++i) {
    if (load <= c[i].x) {
      const float span = c[i].x - c[i - 1].x;
      const float t = span > 0.0f ? (load - c[i - 1].x) / span : 1.0f;
      return c[i - 1].y + t * (c[i].y - c[i - 1].y);
    }
  }
  // Past the last point: keep the last segment's slope (more drills, more trouble).
  const CurvePoint& a = c[c.size() - 2];
  const CurvePoint& b = c.back();
  const float slope = b.x > a.x ? (b.y - a.y) / (b.x - a.x) : 0.0f;
  return b.y + slope * (load - b.x);
}

float instabilityLevel(const InstabilityDef& def, float load, float cooling) {
  const float level = instabilityCurve(def, load) - cooling;
  return level > 0.0f ? level : 0.0f;
}

float coolerCooling(const World& world, uint32_t site) {
  if (world.content == nullptr || site >= world.siteCount()) return 0.0f;
  const BuildingSlot& b = world.site(site);
  if (b.type < 0) return 0.0f;
  const BuildingDef& def = world.content->buildings[static_cast<size_t>(b.type)];
  if (def.kind != BuildingKind::Cooler) return 0.0f;
  const bool lit = (b.flags & (kSlotLit | kSlotShaded)) == kSlotLit;
  return effectiveStats(def, b.tiers).cooling * (lit ? def.litCoolingMult : 1.0f);
}

void drills(World& world, float dt) {
  if (world.content == nullptr || world.planet == nullptr) return;
  const ContentDB& db = *world.content;
  const InstabilityDef& rules = db.rules.instability;
  const uint32_t sites = world.siteCount();

  // Load of the running drills and cooling of the coolers, as things stand.
  float load = 0.0f, cooling = 0.0f;
  for (uint32_t s = 0; s < sites; ++s) {
    const BuildingSlot& b = world.site(s);
    if (b.type < 0) continue;
    const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
    if (def.kind == BuildingKind::Drill) {
      if (b.overheat <= 0.0f) load += effectiveStats(def, b.tiers).instability;
    } else if (def.kind == BuildingKind::Cooler) {
      cooling += coolerCooling(world, s);
    }
  }
  float level = instabilityLevel(rules, load, cooling);

  if (world.running() && dt > 0.0f) {
    for (uint32_t s = 0; s < sites; ++s) {
      BuildingSlot& b = world.site(s);
      if (b.type < 0) continue;
      const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
      if (def.kind != BuildingKind::Drill) continue;
      const float weight = effectiveStats(def, b.tiers).instability;
      if (b.overheat > 0.0f) {
        b.overheat -= dt;
        if (b.overheat > 0.0f) continue;
        b.overheat = 0.0f;
        b.heat = 0.0f;
        load += weight;
        level = instabilityLevel(rules, load, cooling);
        world.emit(EventType::DrillRestarted, static_cast<int32_t>(s), b.type, level);
        continue;
      }
      if (level > rules.threshold) {
        b.heat += (rules.riseBase + rules.risePerExcess * (level - rules.threshold)) * dt;
        if (b.heat >= 1.0f) {
          b.heat = 1.0f;
          b.overheat = rules.overheatSeconds;
          world.emit(EventType::DrillOverheated, static_cast<int32_t>(s), b.type, level, 0.0f,
                     rules.overheatSeconds);
          load -= weight;
          if (load < 0.0f) load = 0.0f;
          level = instabilityLevel(rules, load, cooling);
        }
      } else {
        b.heat -= rules.decayPerSec * dt;
        if (b.heat < 0.0f) b.heat = 0.0f;
      }
    }
  }
  world.instabilityLoad = load;
  world.cooling = cooling;
  world.instability = level;
}

}  // namespace orbitfall
