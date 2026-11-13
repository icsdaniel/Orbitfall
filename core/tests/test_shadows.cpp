// M1.8 acceptance (sim part): the interval maths matches a brute-force ray
// test; shaded panels stop earning; killing the caster clears the shadow; the
// Eclipser flies to the sun line over the lit panels, tracks the sun, hovers,
// descends and lands. (The 1k-caster time budget is checked by core_bench.)
#include <cmath>
#include <vector>

#include "alloc_counter.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "systems/shadows.h"
#include "systems/sun.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

struct Disc {
  float x, y, r;
};

// Brute force: walk the ray from the surface point toward the sun and see
// whether it passes through a disc that stands between the sun and the planet.
// `margin` reports how close the nearest miss/hit was to a disc edge.
bool rayBlocked(const std::vector<Disc>& discs, float px, float py, float sx, float sy, float R,
                float& margin) {
  bool blocked = false;
  margin = 1.0e9f;
  for (const Disc& d : discs) {
    if (!(d.x * sx + d.y * sy > R)) continue;  // not in front of the planet
    // Closest approach of the ray P + t s (t >= 0) to the disc centre.
    const float t = (d.x - px) * sx + (d.y - py) * sy;
    const float cx = px + (t > 0.0f ? t : 0.0f) * sx, cy = py + (t > 0.0f ? t : 0.0f) * sy;
    const float dist = std::hypot(d.x - cx, d.y - cy);
    margin = std::fmin(margin, std::fabs(dist - d.r));
    if (dist <= d.r) blocked = true;
  }
  return blocked;
}

std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

void place(Sim& sim, int slot, int building) {
  sim.world().stardust += 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
  sim.step();
  sim.world().stardust = 0.0f;
}

// A parked Eclipser-type caster at a world position (it will not move: Descend
// with zero descend speed is not available, so the tests freeze it by hand).
uint32_t putCaster(World& w, float x, float y) {
  const uint32_t i = spawnEnemy(w, kEclipser, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  return i;
}

}  // namespace

TEST_CASE("shadows: interval maths matches a brute-force ray test") {
  Rng rng;
  rng.seed(2026);
  const float R = 100.0f;
  int shadedPoints = 0, litPoints = 0, skipped = 0;
  for (int scene = 0; scene < 200; ++scene) {
    const float sigma = rng.range(0.0f, kTau);
    const float sx = std::cos(sigma), sy = std::sin(sigma);
    std::vector<Disc> discs(1 + rng.nextBelow(12));
    std::vector<ShadeInterval> intervals(discs.size());
    uint32_t n = 0;
    for (Disc& d : discs) {
      const float a = rng.range(0.0f, kTau), dist = rng.range(170.0f, 700.0f);
      d = {dist * std::cos(a), dist * std::sin(a), rng.range(3.0f, 60.0f)};
      if (casterInterval(d.x, d.y, d.r, sx, sy, R, intervals[n])) ++n;
    }
    n = mergeIntervals(intervals.data(), n);
    for (uint32_t k = 1; k < n; ++k) CHECK(intervals[k].lo > intervals[k - 1].hi);  // merged, sorted

    for (int p = 0; p < 360; ++p) {
      const float phi = kTau * (static_cast<float>(p) + 0.37f) / 360.0f;
      if (!(std::cos(phi - sigma) > 0.0f)) continue;  // night side: not a lit point
      float margin = 0.0f;
      const bool want = rayBlocked(discs, R * std::cos(phi), R * std::sin(phi), sx, sy, R, margin);
      if (margin < 0.01f) {  // on a disc edge: either answer is right
        ++skipped;
        continue;
      }
      const bool got = isShaded(intervals.data(), n, R * std::sin(phi - sigma));
      CHECK(got == want);
      want ? ++shadedPoints : ++litPoints;
    }
  }
  CHECK(shadedPoints > 2000);
  CHECK(litPoints > 2000);
  CHECK(skipped < 200);
}

TEST_CASE("shadows: a caster casts only from between the sun and the planet") {
  ShadeInterval iv;
  // Sun along +x, planet radius 100.
  CHECK(casterInterval(260.0f, 10.0f, 28.0f, 1.0f, 0.0f, 100.0f, iv));
  CHECK(iv.lo == doctest::Approx(-18.0f));
  CHECK(iv.hi == doctest::Approx(38.0f));
  CHECK_FALSE(casterInterval(-260.0f, 0.0f, 28.0f, 1.0f, 0.0f, 100.0f, iv));  // behind the planet
  CHECK_FALSE(casterInterval(90.0f, 150.0f, 28.0f, 1.0f, 0.0f, 100.0f, iv));  // beside it: dot <= R
  CHECK_FALSE(casterInterval(260.0f, 140.0f, 28.0f, 1.0f, 0.0f, 100.0f, iv)); // shadow misses the planet
}

TEST_CASE("shadows: overlapping intervals merge, separate ones stay") {
  ShadeInterval v[5] = {{10.0f, 20.0f}, {-30.0f, -20.0f}, {18.0f, 25.0f}, {25.0f, 26.0f}, {40.0f, 50.0f}};
  REQUIRE(mergeIntervals(v, 5) == 3);
  CHECK(v[0].lo == -30.0f);
  CHECK(v[0].hi == -20.0f);
  CHECK(v[1].lo == 10.0f);
  CHECK(v[1].hi == 26.0f);
  CHECK(v[2].lo == 40.0f);
  CHECK(isShaded(v, 3, 15.0f));
  CHECK(isShaded(v, 3, 26.0f));
  CHECK_FALSE(isShaded(v, 3, 30.0f));
  CHECK_FALSE(isShaded(v, 3, -31.0f));
  CHECK_FALSE(isShaded(v, 0, 0.0f));
}

TEST_CASE("shadows: 1k casters are handled without allocating") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  Rng rng;
  rng.seed(5);
  for (int i = 0; i < 1000; ++i) {
    const float a = rng.range(-1.0f, 1.0f), d = rng.range(300.0f, 800.0f);
    putCaster(w, d * std::cos(a), d * std::sin(a));
  }
  sim.step();
  const uint64_t before = allocationCount();
  sim.step();
  CHECK(allocationCount() == before);
  CHECK(w.shadeCount >= 1);
  CHECK(w.shadeCount <= 1000);
}

TEST_CASE("shadows: a shaded panel earns nothing and killing the caster clears the shadow") {
  ContentDB db = tinyContent();
  db.suns[0].type = SunPathType::Fixed;  // sun stays at 0 deg
  db.enemies[kEclipser].speed = 0.001f;  // parked
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 0, kPanel);  // on the sun line
  place(sim, 4, kPanel);  // 40 deg: y = 64, outside a shadow of half-width 28
  const uint32_t caster = putCaster(w, 260.0f, 0.0f);
  sim.step();
  CHECK((w.slots[0].flags & (kSlotLit | kSlotShaded)) == (kSlotLit | kSlotShaded));
  CHECK((w.slots[1].flags & kSlotShaded) != 0);   // y = 17.4
  CHECK((w.slots[2].flags & kSlotShaded) == 0);   // y = 34.2
  CHECK((w.slots[4].flags & (kSlotLit | kSlotShaded)) == kSlotLit);
  CHECK((w.slots[18].flags & kSlotShaded) == 0);  // night side is unlit, not "shaded"
  const auto changed = eventsOf(sim, EventType::ShadowChanged);
  REQUIRE(changed.size() == 1);
  CHECK(changed[0].a == 3);  // slots 35, 0, 1
  CHECK(changed[0].b == 1);

  // The wedge for the client: one interval of +-asin(28/100) around the sun line.
  float angles[8];
  REQUIRE(shadeIntervalAngles(w, angles, 4) == 1);
  CHECK(angles[0] == doctest::Approx(-std::asin(0.28f)).epsilon(0.001));
  CHECK(angles[1] == doctest::Approx(std::asin(0.28f)).epsilon(0.001));

  for (int i = 0; i < 300; ++i) sim.step();
  CHECK(w.stardust == doctest::Approx(30.0f).epsilon(0.01));  // only the unshaded panel
  CHECK(w.incomePerSecond == doctest::Approx(3.0f));

  // Kill it: next tick the shadow is gone and both panels earn.
  w.stardust = 0.0f;
  REQUIRE(damageEnemy(w, caster, 1000.0f));
  w.stardust = 0.0f;  // drop the bounty
  sim.step();
  CHECK(w.shadeCount == 0);
  CHECK((w.slots[0].flags & kSlotShaded) == 0);
  CHECK(eventsOf(sim, EventType::ShadowChanged).size() == 2);
  CHECK(shadeIntervalAngles(w, angles, 4) == 0);
  for (int i = 0; i < 299; ++i) sim.step();
  CHECK(w.stardust == doctest::Approx(60.0f).epsilon(0.01));
}

TEST_CASE("shadows: small enemies cast none") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = spawnEnemy(w, kGreen, 0);
  w.enemies.posX[i] = 300.0f;
  w.enemies.posY[i] = 0.0f;
  sim.step();
  CHECK(w.shadeCount == 0);
  for (const BuildingSlot& b : w.slots) CHECK((b.flags & kSlotShaded) == 0);
}

TEST_CASE("eclipser: parks where its shadow covers the most lit panels") {
  ContentDB db = tinyContent();
  db.suns[0].type = SunPathType::Fixed;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  CHECK(eclipserOffset(w, 28.0f) == 0.0f);  // no panels: the middle of the beam
  place(sim, 33, kPanel);                   // -30 deg, y = -50: alone
  place(sim, 2, kPanel);                    // 20 deg, y = 34.2
  place(sim, 3, kPanel);                    // 30 deg, y = 50
  place(sim, 4, kPanel);                    // 40 deg, y = 64.3
  place(sim, 18, kPanel);                   // night side: ignored
  // A shadow 56 wide fits the three panels at 34..64; centred on them.
  CHECK(eclipserOffset(w, 28.0f) == doctest::Approx(0.5f * (34.202f + 64.279f)).epsilon(0.001));
  // A narrow one covers two neighbours at best (16 apart); the pair nearest the sun line wins.
  CHECK(eclipserOffset(w, 9.0f) == doctest::Approx(0.5f * (34.202f + 50.0f)).epsilon(0.001));
  // Turning the planet moves the panels, and the spot with them.
  w.planetRotation = 180.0f * kDeg;
  CHECK(eclipserOffset(w, 28.0f) == doctest::Approx(0.0f).epsilon(0.001));  // only slot 18 is lit, at y = 0
}

TEST_CASE("eclipser: flies to the hover radius on the sun line, tracks the sun, hovers, descends, lands") {
  ContentDB db = tinyContent();
  db.rules.killBounty = 0.0f;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 2, kPanel);
  place(sim, 3, kPanel);
  place(sim, 4, kPanel);
  const uint32_t i = spawnEnemy(w, kEclipser, 0);
  REQUIRE(i == 0);
  const float spawnBearing = std::atan2(w.enemies.posY[0], w.enemies.posX[0]);
  CHECK(std::fabs(spawnBearing - 30.0f * kDeg) <= 6.0f * kDeg + 1e-4f);  // lane bearing +- jitter
  CHECK(std::hypot(w.enemies.posX[0], w.enemies.posY[0]) == doctest::Approx(900.0f));
  CHECK(eclipserState(w.enemies.flags[0]) == EclipserState::Approach);

  // Approach at its speed (25/s): about 640 units, under 40 s.
  uint32_t ticks = 0;
  while (eclipserState(w.enemies.flags[0]) == EclipserState::Approach && ticks < 2000) {
    const float x = w.enemies.posX[0], y = w.enemies.posY[0];
    sim.step();
    ++ticks;
    CHECK(std::hypot(w.enemies.posX[0] - x, w.enemies.posY[0] - y) <= 25.0f * kFixedDt * 1.001f);
  }
  REQUIRE(eclipserState(w.enemies.flags[0]) == EclipserState::Hover);
  CHECK(ticks > 20 * 30);
  CHECK(ticks < 40 * 30);

  // Hovering: on the sun line at the hover radius, offset over the panels, and
  // it keeps that station while the sun drifts.
  const auto station = [&](float& along, float& across) {
    const float sx = std::cos(w.sunBearing), sy = std::sin(w.sunBearing);
    along = w.enemies.posX[0] * sx + w.enemies.posY[0] * sy;
    across = sx * w.enemies.posY[0] - sy * w.enemies.posX[0];
  };
  float along = 0.0f, across = 0.0f;
  const float sunAtArrival = w.sunBearing;
  uint32_t hoverTicks = 0;
  while (eclipserState(w.enemies.flags[0]) == EclipserState::Hover && hoverTicks < 2000) {
    sim.step();
    ++hoverTicks;
    if (eclipserState(w.enemies.flags[0]) != EclipserState::Hover) break;
    station(along, across);
    CHECK(along == doctest::Approx(260.0f).epsilon(0.01));
    CHECK(across == doctest::Approx(eclipserOffset(w, 28.0f)).epsilon(0.02));
    // Its shadow covers the panels it came for, so they earn nothing.
    if (hoverTicks > 2) CHECK(w.incomePerSecond < 9.0f);
  }
  CHECK(hoverTicks == doctest::Approx(15 * 30).epsilon(0.01));  // hoverSeconds
  CHECK(w.sunBearing > sunAtArrival + 20.0f * kDeg);            // 15 s at 1.5 deg/s: it tracked a moving sun
  REQUIRE(eclipserState(w.enemies.flags[0]) == EclipserState::Descend);

  // Descends at descendSpeed straight toward the planet, then lands for 15.
  const float r0 = std::hypot(w.enemies.posX[0], w.enemies.posY[0]);
  for (int k = 0; k < 30; ++k) sim.step();
  CHECK(r0 - std::hypot(w.enemies.posX[0], w.enemies.posY[0]) == doctest::Approx(10.0f).epsilon(0.01));
  uint32_t fall = 0;
  while (w.enemies.count > 0 && fall < 3000) {
    sim.step();
    ++fall;
  }
  CHECK(w.enemies.count == 0);
  const auto hits = eventsOf(sim, EventType::PlanetHit);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].a == kEclipser);
  CHECK(hits[0].value == 15.0f);
  CHECK(w.planetHp == 85.0f);
  sim.step();
  CHECK(w.shadeCount == 0);  // landed: its shadow is gone too
}

TEST_CASE("eclipser: follows the panels when the player turns the planet") {
  ContentDB db = tinyContent();
  db.suns[0].type = SunPathType::Fixed;
  db.enemies[kEclipser].hover.hoverSeconds = 1000.0f;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 3, kPanel);  // 30 deg, y = 50
  const uint32_t i = putCaster(w, 260.0f, 50.0f);
  for (int k = 0; k < 10; ++k) sim.step();
  CHECK(w.enemies.posY[i] == doctest::Approx(50.0f).epsilon(0.01));
  CHECK((w.slots[3].flags & kSlotShaded) != 0);
  // Turn the panel to -30 deg (y = -50): out of the shadow at once ...
  w.planetRotation = wrapTau(-60.0f * kDeg);
  sim.step();
  sim.step();
  CHECK((w.slots[3].flags & (kSlotLit | kSlotShaded)) == kSlotLit);
  // ... and the Eclipser needs 100 / 25 = 4 s to slide back over it.
  for (int k = 0; k < 60; ++k) sim.step();
  CHECK((w.slots[3].flags & kSlotShaded) == 0);
  for (int k = 0; k < 90; ++k) sim.step();
  CHECK((w.slots[3].flags & kSlotShaded) != 0);
  CHECK(w.enemies.posY[i] == doctest::Approx(-50.0f).epsilon(0.01));
}
