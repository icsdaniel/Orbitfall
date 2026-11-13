#include "systems/shadows.h"

#include <algorithm>
#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/enemies.h"
#include "systems/sun.h"

namespace orbitfall {

bool casterInterval(float px, float py, float r, float sx, float sy, float planetRadius,
                    ShadeInterval& out) {
  if (!(px * sx + py * sy > planetRadius)) return false;  // not between sun and planet
  const float o = sx * py - sy * px;
  if (o - r > planetRadius || o + r < -planetRadius) return false;  // misses the planet
  out.lo = o - r;
  out.hi = o + r;
  return true;
}

uint32_t mergeIntervals(ShadeInterval* v, uint32_t count) {
  if (count < 2) return count;
  std::sort(v, v + count, [](const ShadeInterval& a, const ShadeInterval& b) {
    return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi);
  });
  uint32_t n = 0;
  for (uint32_t i = 1; i < count; ++i) {
    if (v[i].lo <= v[n].hi) {
      if (v[i].hi > v[n].hi) v[n].hi = v[i].hi;
    } else {
      v[++n] = v[i];
    }
  }
  return n + 1;
}

bool isShaded(const ShadeInterval* v, uint32_t count, float y) {
  // Binary search for the last interval starting at or before y.
  uint32_t lo = 0, hi = count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (v[mid].lo <= y) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo > 0 && y <= v[lo - 1].hi;
}

uint32_t shadeIntervalAngles(const World& world, float* out, uint32_t maxPairs) {
  if (world.planet == nullptr || world.sun == nullptr) return 0;
  const float radius = world.planet->radius;
  const float half = 0.5f * world.sun->beamWidth < radius ? 0.5f * world.sun->beamWidth : radius;
  uint32_t n = 0;
  for (uint32_t i = 0; i < world.shadeCount && n < maxPairs; ++i) {
    const float lo = world.shade[i].lo > -half ? world.shade[i].lo : -half;
    const float hi = world.shade[i].hi < half ? world.shade[i].hi : half;
    if (lo >= hi) continue;  // outside the beam
    out[2 * n] = world.sunBearing + std::asin(clampf(lo / radius, -1.0f, 1.0f));
    out[2 * n + 1] = world.sunBearing + std::asin(clampf(hi / radius, -1.0f, 1.0f));
    ++n;
  }
  return n;
}

float eclipserOffset(const World& world, float casterRadius) {
  if (world.content == nullptr || world.planet == nullptr || world.sun == nullptr) return 0.0f;
  const float radius = world.planet->radius, width = world.sun->beamWidth;
  const uint32_t n = world.slotCount();
  const float span = 2.0f * casterRadius;

  // y of slot `s` if it holds a lit panel.
  const auto litPanelY = [&](uint32_t s, float& y) {
    const BuildingSlot& b = world.slots[s];
    if (b.type < 0) return false;
    if (world.content->buildings[static_cast<size_t>(b.type)].kind != BuildingKind::Panel) return false;
    const float angle = world.slotAngle(s) + world.planetRotation;
    if (!inBeam(radius, width, world.sunBearing, angle)) return false;
    y = radius * std::sin(angle - world.sunBearing);
    return true;
  };

  // Try a shadow starting at each lit panel; slots are few, so O(n^2) is fine.
  int bestCount = 0;
  float bestCentre = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    float start = 0.0f;
    if (!litPanelY(i, start)) continue;
    int count = 0;
    float top = start;
    for (uint32_t j = 0; j < n; ++j) {
      float y = 0.0f;
      if (!litPanelY(j, y) || y < start || y > start + span) continue;
      ++count;
      if (y > top) top = y;
    }
    const float centre = 0.5f * (start + top);
    // More panels wins; ties go to the spot nearest the sun line.
    if (count > bestCount || (count == bestCount && std::fabs(centre) < std::fabs(bestCentre))) {
      bestCount = count;
      bestCentre = centre;
    }
  }
  return bestCentre;
}

}  // namespace orbitfall
