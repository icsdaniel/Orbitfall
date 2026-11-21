// M2.11 focus-point solver and focus bonus (see focus.h).
#include "systems/focus.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/upgrades.h"

namespace orbitfall {
namespace {

// Aim-mounted shooting towers take part in focus fire.
bool feedsFocus(const ContentDB& db, const BuildingSlot& b) {
  if (b.type < 0) return false;
  const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
  return def.kind == BuildingKind::Tower && def.hasProjectile && def.aimMount;
}

void addFeeder(FocusPoint& p, uint32_t site, bool lens) {
  for (int k = 0; k < p.feeders; ++k)
    if (p.sites[k] == site) return;
  if (p.feeders >= kMaxFocusFeeders) return;
  p.sites[p.feeders++] = static_cast<uint16_t>(site);
  if (lens) ++p.lenses;
}

bool isLens(const ContentDB& db, const BuildingSlot& b) {
  return db.buildings[static_cast<size_t>(b.type)].projectile.kind == ProjectileKind::Beam;
}

}  // namespace

void solveFocus(World& world) {
  world.focus.clear();
  if (world.content == nullptr || world.planet == nullptr) return;
  const ContentDB& db = *world.content;
  const FocusRulesDef& rules = db.rules.focus;
  const float minDist = world.planet->radius + rules.minAltitude;
  const uint32_t n = world.siteCount();
  for (uint32_t a = 0; a < n; ++a) {
    const BuildingSlot& ba = world.site(a);
    if (!feedsFocus(db, ba)) continue;
    const float angA = world.siteAngle(a), rA = world.siteRadius(a);
    const float ax = rA * std::cos(angA), ay = rA * std::sin(angA);
    const float dax = std::cos(angA + ba.aimOffset), day = std::sin(angA + ba.aimOffset);
    const float rangeA = slotStats(db.buildings[static_cast<size_t>(ba.type)], ba).range;
    for (uint32_t b = a + 1; b < n; ++b) {
      const BuildingSlot& bb = world.site(b);
      if (!feedsFocus(db, bb)) continue;
      const float angB = world.siteAngle(b), rB = world.siteRadius(b);
      const float bx = rB * std::cos(angB), by = rB * std::sin(angB);
      const float dbx = std::cos(angB + bb.aimOffset), dby = std::sin(angB + bb.aimOffset);
      const float cross = dax * dby - day * dbx;
      if (std::fabs(cross) < 1.0e-4f) continue;  // parallel axes never meet
      const float ex = bx - ax, ey = by - ay;
      const float alongA = (ex * dby - ey * dbx) / cross;
      const float alongB = (ex * day - ey * dax) / cross;
      const float rangeB = slotStats(db.buildings[static_cast<size_t>(bb.type)], bb).range;
      if (!(alongA > 0.0f) || !(alongB > 0.0f) || alongA > rangeA || alongB > rangeB) continue;
      const float px = ax + dax * alongA, py = ay + day * alongA;
      if (px * px + py * py < minDist * minDist) continue;
      FocusPoint* into = nullptr;
      for (FocusPoint& f : world.focus) {
        const float dx = f.x - px, dy = f.y - py;
        if (dx * dx + dy * dy < rules.radius * rules.radius) {
          into = &f;
          break;
        }
      }
      if (into == nullptr) {
        if (world.focus.size() >= kMaxFocusPoints) continue;
        world.focus.push_back(FocusPoint());  // reserved: no allocation
        into = &world.focus.back();
        into->radius = rules.radius;
      }
      const float w = static_cast<float>(into->pairs);
      into->x = (into->x * w + px) / (w + 1.0f);
      into->y = (into->y * w + py) / (w + 1.0f);
      ++into->pairs;
      addFeeder(*into, a, isLens(db, ba));
      addFeeder(*into, b, isLens(db, bb));
    }
  }
}

float focusMultiplier(const FocusRulesDef& rules, int feeders) {
  if (feeders < 2) return 1.0f;
  const float bonus = rules.bonusPerExtra * static_cast<float>(feeders - 1);
  return 1.0f + (bonus < rules.cap ? bonus : rules.cap);
}

int focusFeedersAt(const World& world, uint32_t site, float wx, float wy) {
  if (world.focus.empty()) return 0;
  // World -> planet-local: rotate by -rotation.
  const float c = std::cos(world.planetRotation), s = std::sin(world.planetRotation);
  const float lx = wx * c + wy * s, ly = -wx * s + wy * c;
  int best = 0;
  for (const FocusPoint& f : world.focus) {
    if (f.feeders <= best) continue;
    const float dx = lx - f.x, dy = ly - f.y;
    if (dx * dx + dy * dy > f.radius * f.radius) continue;
    for (int k = 0; k < f.feeders; ++k) {
      if (f.sites[k] == site) {
        best = f.feeders;
        break;
      }
    }
  }
  return best;
}

float focusBonusAt(const World& world, uint32_t site, float wx, float wy) {
  if (world.focus.empty() || world.content == nullptr) return 1.0f;
  return focusMultiplier(world.content->rules.focus, focusFeedersAt(world, site, wx, wy));
}

int feederZones(const World& world, uint32_t site, float* x, float* y, float* r2) {
  if (world.focus.empty()) return 0;
  const float c = std::cos(world.planetRotation), s = std::sin(world.planetRotation);
  int n = 0;
  for (const FocusPoint& f : world.focus) {
    if (n >= kMaxZonesPerTower) break;
    for (int k = 0; k < f.feeders; ++k) {
      if (f.sites[k] != site) continue;
      x[n] = f.x * c - f.y * s;
      y[n] = f.x * s + f.y * c;
      r2[n] = f.radius * f.radius;
      ++n;
      break;
    }
  }
  return n;
}

void focusWorldPos(const World& world, uint32_t i, float& wx, float& wy) {
  const FocusPoint& f = world.focus[i];
  const float c = std::cos(world.planetRotation), s = std::sin(world.planetRotation);
  wx = f.x * c - f.y * s;
  wy = f.x * s + f.y * c;
}

float snapAimOffset(const RulesDef& rules, float offset) {
  const float limit = kMaxAimOffsetDeg * kPi / 180.0f;
  if (!(offset - offset == 0.0f)) return 0.0f;  // NaN / inf
  offset = clampf(offset, -limit, limit);
  if (rules.aim.offsets.empty()) return offset;
  float best = rules.aim.offsets.front();
  for (const float o : rules.aim.offsets)
    if (std::fabs(o - offset) < std::fabs(best - offset)) best = o;
  return best;
}

float reaimCost(const RulesDef& rules, const BuildingDef& def) {
  return def.cost * rules.aim.reaimCostFrac;
}

}  // namespace orbitfall
