// M1.7 acceptance (sim part): lit earns, unlit doesn't; a second SunPath type
// swaps in; the lit arc formula; income = panelRate x beamStrength.
#include <cmath>

#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/sun.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

void place(Sim& sim, int slot, int building) {
  sim.world().stardust += 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
  sim.step();
  sim.world().stardust = 0.0f;
}

}  // namespace

TEST_CASE("beam: the test is cos > 0 and |R sin| <= W/2") {
  // R 100, W 141: edge at asin(0.705) = 44.83 deg.
  CHECK(inBeam(100.0f, 141.0f, 0.0f, 0.0f));
  CHECK(inBeam(100.0f, 141.0f, 0.0f, 44.0f * kDeg));
  CHECK(inBeam(100.0f, 141.0f, 0.0f, -44.0f * kDeg));
  CHECK_FALSE(inBeam(100.0f, 141.0f, 0.0f, 46.0f * kDeg));
  CHECK_FALSE(inBeam(100.0f, 141.0f, 0.0f, 180.0f * kDeg));  // night side, |R sin| = 0
  CHECK_FALSE(inBeam(100.0f, 141.0f, 0.0f, 150.0f * kDeg));  // night side, inside the band
  CHECK(inBeam(100.0f, 141.0f, 350.0f * kDeg, 20.0f * kDeg));  // across the wrap
  CHECK(inBeam(100.0f, 250.0f, 0.0f, 89.0f * kDeg));           // wider than the planet
  CHECK_FALSE(inBeam(100.0f, 250.0f, 0.0f, 91.0f * kDeg));
}

TEST_CASE("beam: the lit arc is 2 asin(W / 2R), about 90 degrees on the medium planet") {
  CHECK(litArc(100.0f, 141.0f) == doctest::Approx(2.0f * std::asin(0.705f)));
  CHECK(litArc(100.0f, 141.0f) / kDeg == doctest::Approx(90.0f).epsilon(0.01));
  CHECK(litArc(100.0f, 100.0f) / kDeg == doctest::Approx(60.0f));
  CHECK(litArc(100.0f, 400.0f) / kDeg == doctest::Approx(180.0f));
  // The beam test agrees: measure the lit arc by sampling the surface.
  for (const float width : {60.0f, 100.0f, 141.0f, 190.0f}) {
    int lit = 0;
    const int samples = 36000;
    for (int i = 0; i < samples; ++i) {
      if (inBeam(100.0f, width, 1.0f, kTau * static_cast<float>(i) / samples)) ++lit;
    }
    CHECK(kTau * static_cast<float>(lit) / samples == doctest::Approx(litArc(100.0f, width)).epsilon(0.002));
  }
}

TEST_CASE("sun: slots in the beam are flagged lit and follow the planet's rotation") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  sim.step();
  // Sun at 0 deg, slots every 10 deg: -40..40 lit = 9 slots.
  int lit = 0;
  for (const BuildingSlot& b : w.slots) lit += (b.flags & kSlotLit) ? 1 : 0;
  CHECK(lit == 9);
  CHECK((w.slots[4].flags & kSlotLit) != 0);
  CHECK((w.slots[5].flags & kSlotLit) == 0);
  CHECK((w.slots[32].flags & kSlotLit) != 0);
  CHECK((w.slots[18].flags & kSlotLit) == 0);
  // Turn the planet 180 deg: the far side is lit now.
  w.planetRotation = kPi;
  sim.step();
  CHECK((w.slots[18].flags & kSlotLit) != 0);
  CHECK((w.slots[0].flags & kSlotLit) == 0);
}

TEST_CASE("income: a lit panel earns panelRate x beamStrength, an unlit one nothing") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 0, kPanel);   // lit
  place(sim, 18, kPanel);  // night side
  for (int i = 0; i < 300; ++i) sim.step();  // 10 s
  CHECK(w.stardust == doctest::Approx(30.0f).epsilon(0.001));
  CHECK(w.incomePerSecond == doctest::Approx(3.0f));

  // Only the unlit one: nothing.
  Sim dark(sandboxConfig(db));
  place(dark, 18, kPanel);
  for (int i = 0; i < 300; ++i) dark.step();
  CHECK(dark.world().stardust == 0.0f);
  CHECK(dark.world().incomePerSecond == 0.0f);

  // Towers in the beam earn nothing.
  Sim tower(sandboxConfig(db));
  place(tower, 0, kGatling);
  for (int i = 0; i < 30; ++i) tower.step();
  CHECK(tower.world().stardust == 0.0f);
}

TEST_CASE("income: scales with base strength and the wave's light level; frozen when paused") {
  ContentDB db = tinyContent();
  db.suns[0].baseStrength = 2.0f;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 0, kPanel);
  w.lightMult = 0.5f;
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.beamStrength == doctest::Approx(1.0f));
  CHECK(w.stardust == doctest::Approx(3.0f).epsilon(0.001));
  w.lightMult = 0.25f;
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.stardust == doctest::Approx(4.5f).epsilon(0.001));
  sim.push(cmd::setTimeScale(sim.tick(), 0.0f));
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.stardust == doctest::Approx(4.5f).epsilon(0.001));
}

TEST_CASE("income: a panel stops earning when the sun drifts off it") {
  ContentDB db = tinyContent();
  db.suns[0].speed = 30.0f * kDeg;  // fast sun: leaves slot 0 after about 1.5 s
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  place(sim, 0, kPanel);
  for (int i = 0; i < 90; ++i) sim.step();
  const float earned = w.stardust;
  CHECK(earned > 3.0f);
  CHECK(earned < 6.0f);
  CHECK((w.slots[0].flags & kSlotLit) == 0);
  for (int i = 0; i < 60; ++i) sim.step();
  CHECK(w.stardust == earned);
  // StardustChanged fires per whole unit, not per tick.
  uint32_t changes = 0;
  for (const Event& e : w.events.items) changes += e.type == EventType::StardustChanged ? 1 : 0;
  CHECK(changes == 1 + static_cast<uint32_t>(earned));  // the placement, then one per unit
}

TEST_CASE("sun path: constant drift moves at speed; a second type swaps in from content") {
  ContentDB db = tinyContent();
  {
    Sim sim(sandboxConfig(db));
    for (int i = 0; i < 300; ++i) sim.step();
    CHECK(sim.world().sunBearing == doctest::Approx(15.0f * kDeg).epsilon(0.001));
  }
  db.suns[0].type = SunPathType::Fixed;
  db.suns[0].start = 200.0f * kDeg;
  {
    Sim sim(sandboxConfig(db));
    CHECK(sim.world().sunPath == &sunPathFor(SunPathType::Fixed));
    for (int i = 0; i < 300; ++i) sim.step();
    CHECK(sim.world().sunBearing == doctest::Approx(200.0f * kDeg));
    CHECK((sim.world().slots[20].flags & kSlotLit) != 0);
  }
  // Anything with the SunPath shape plugs in without touching a system.
  static const SunPath backwards = {"backwards", [](const SunDef& d, float bearing, float dt) {
                                      return wrapTau(bearing - 2.0f * d.speed * dt);
                                    }};
  db.suns[0].type = SunPathType::ConstantDrift;
  db.suns[0].start = 0.0f;
  Sim sim(sandboxConfig(db));
  sim.world().sunPath = &backwards;
  for (int i = 0; i < 300; ++i) sim.step();
  CHECK(sim.world().sunBearing == doctest::Approx(330.0f * kDeg).epsilon(0.001));
}

TEST_CASE("sun path: editing the sun speed changes the drift live") {
  ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  for (int i = 0; i < 30; ++i) sim.step();
  const float a = sim.world().sunBearing;
  db.suns[0].speed = 15.0f * kDeg;  // what the tuning panel does
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(sim.world().sunBearing - a == doctest::Approx(15.0f * kDeg).epsilon(0.001));
}
