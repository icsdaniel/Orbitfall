// M2.7 acceptance: drill output is independent of light; instability past the
// threshold makes a drill overheat and shut down for a cooldown, with no
// damage to any building; a Cooler lowers it. Tiny worlds with fixed numbers.
#include <vector>

#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/drills.h"
#include "systems/upgrades.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

constexpr float kDrillRate = 1.5f;

// tinyContent plus a drill (index 3) and a cooler (index 4).
ContentDB drillContent(float sunStrength = 1.0f) {
  ContentDB db = tinyContent();
  db.suns[0].speed = 0.0f;  // the sun stays at 0 deg: slot 0 lit, slot 18 dark
  db.suns[0].baseStrength = sunStrength;
  InstabilityDef& in = db.rules.instability;
  in.curve = {{0, 0}, {1, 0.2f}, {2, 0.45f}, {3, 0.8f}, {4, 1.2f}, {6, 2.2f}};
  in.threshold = 1.0f;
  in.riseBase = 0.1f;
  in.risePerExcess = 0.5f;
  in.decayPerSec = 0.1f;
  in.overheatSeconds = 5.0f;

  BuildingDef drill;
  drill.id = "drill";
  drill.kind = BuildingKind::Drill;
  drill.cost = 70.0f;
  drill.drillRate = kDrillRate;
  drill.instability = 1.0f;
  UpgradePathDef bore{"bore", "Deep Bore", {{"Bit", 40.0f, {{UpgradeStat::Income, ModOp::Add, 0.5f}}}}};
  UpgradePathDef calm{"stable", "Stable", {{"Damped", 40.0f, {{UpgradeStat::Instability, ModOp::Mul, 0.5f}}}}};
  drill.paths = {bore, calm};
  db.buildings.push_back(drill);

  BuildingDef cooler;
  cooler.id = "cooler";
  cooler.kind = BuildingKind::Cooler;
  cooler.layer = BuildingLayer::Any;
  cooler.cost = 60.0f;
  cooler.cooling = 0.4f;
  cooler.litCoolingMult = 0.5f;
  UpgradePathDef fins{"radiator", "Radiators", {{"Fins", 30.0f, {{UpgradeStat::Cooling, ModOp::Mul, 1.5f}}}}};
  cooler.paths = {fins};
  db.buildings.push_back(cooler);
  for (size_t i = 0; i < db.buildings.size(); ++i) db.buildingIds[db.buildings[i].id] = static_cast<int>(i);
  return db;
}
constexpr int kDrill = 3, kCooler = 4;

// Places for free and leaves the bank at 0.
void place(Sim& sim, int slot, int building) {
  sim.world().stardust += 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
  sim.step();
  sim.world().stardust = 0.0f;
  sim.world().events.clear();
}

struct Tally {
  int overheated = 0, restarted = 0, damaging = 0;
  std::vector<int> overheatedSites;
};

// Steps `ticks` ticks and counts the M2.7 events plus anything that would mean damage.
Tally run(Sim& sim, int ticks) {
  Tally t;
  for (int i = 0; i < ticks; ++i) {
    sim.step();
    for (const Event& e : sim.world().events.items) {
      if (e.type == EventType::DrillOverheated) {
        ++t.overheated;
        t.overheatedSites.push_back(e.a);
      }
      if (e.type == EventType::DrillRestarted) ++t.restarted;
      if (e.type == EventType::BuildingSold || e.type == EventType::PlanetHit) ++t.damaging;
    }
    sim.world().events.clear();
  }
  return t;
}

int drillsRunning(const World& w) {
  int n = 0;
  for (const BuildingSlot& b : w.slots) n += (b.type == kDrill && b.overheat <= 0.0f) ? 1 : 0;
  return n;
}

}  // namespace

TEST_CASE("instability curve: linear between points, extended past the last, never negative") {
  InstabilityDef d;
  CHECK(instabilityCurve(d, 3.0f) == 0.0f);  // no curve: drills never destabilise
  d.curve = {{0, 0}, {2, 1.0f}, {4, 3.0f}};
  CHECK(instabilityCurve(d, 0.0f) == doctest::Approx(0.0f));
  CHECK(instabilityCurve(d, 1.0f) == doctest::Approx(0.5f));
  CHECK(instabilityCurve(d, 3.0f) == doctest::Approx(2.0f));
  CHECK(instabilityCurve(d, 6.0f) == doctest::Approx(5.0f));  // slope 1 past the last point
  CHECK(instabilityLevel(d, 3.0f, 0.5f) == doctest::Approx(1.5f));
  CHECK(instabilityLevel(d, 1.0f, 2.0f) == 0.0f);
}

TEST_CASE("drill: output is the same lit, dark, in a dim beam and with no sun at all") {
  for (const float strength : {1.0f, 0.3f, 0.0f}) {
    const ContentDB db = drillContent(strength);
    for (const int slot : {0, 18}) {  // slot 0 in the beam, slot 18 on the night side
      Sim sim(sandboxConfig(db));
      place(sim, slot, kDrill);
      const World& w = sim.world();
      CHECK(((w.slots[static_cast<size_t>(slot)].flags & kSlotLit) != 0) == (slot == 0));
      run(sim, 300);  // 10 s
      CHECK(w.stardust == doctest::Approx(10.0f * kDrillRate).epsilon(0.01));
      CHECK(w.drillIncomePerSecond == doctest::Approx(kDrillRate));
      CHECK(w.incomePerSecond == 0.0f);  // panel income stays a separate number
    }
  }
  // Control: a panel on the night side earns nothing.
  const ContentDB db = drillContent();
  Sim sim(sandboxConfig(db));
  place(sim, 18, kPanel);
  run(sim, 300);
  CHECK(sim.world().stardust == 0.0f);
}

TEST_CASE("drill: below the threshold no drill ever overheats") {
  const ContentDB db = drillContent();
  Sim sim(sandboxConfig(db));
  for (const int slot : {20, 21, 22}) place(sim, slot, kDrill);  // curve(3) = 0.8 < 1
  const Tally t = run(sim, 30 * 120);
  CHECK(sim.world().instability == doctest::Approx(0.8f));
  CHECK(t.overheated == 0);
  CHECK(sim.world().stardust == doctest::Approx(120.0f * 3.0f * kDrillRate).epsilon(0.01));
}

TEST_CASE("drill: past the threshold a drill overheats, shuts down for the cooldown, and nothing is damaged") {
  const ContentDB db = drillContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const std::vector<int> slots = {20, 21, 22, 23};
  for (const int slot : slots) place(sim, slot, kDrill);
  sim.step();
  CHECK(w.instability == doctest::Approx(1.2f));  // curve(4) above the threshold of 1
  const float hpBefore = w.planetHp;

  // Heat rises at 0.1 + 0.5 * 0.2 = 0.2/s: the first overheat comes after ~5 s.
  Tally t = run(sim, 30 * 4);
  CHECK(t.overheated == 0);
  CHECK(w.slots[20].heat > 0.7f);
  t = run(sim, 30 * 2);
  // All four heated together, but one shutdown takes the level back to
  // curve(3) = 0.8, so exactly one drill stops (the first in site order).
  REQUIRE(t.overheated == 1);
  CHECK(t.overheatedSites[0] == 20);
  CHECK(drillsRunning(w) == 3);
  CHECK(w.instability == doctest::Approx(0.8f));
  CHECK(w.slots[20].overheat > 0.0f);
  CHECK(w.drillIncomePerSecond == doctest::Approx(3.0f * kDrillRate));  // the stopped drill earns nothing

  // Nothing is damaged: every building stands, untouched, and the planet keeps its HP.
  for (const int slot : slots) {
    CHECK(w.slots[static_cast<size_t>(slot)].type == kDrill);
    CHECK(w.slots[static_cast<size_t>(slot)].anchor == slot);
  }
  CHECK(w.planetHp == hpBefore);
  CHECK(t.damaging == 0);

  // After the cooldown (5 s) it restarts with no heat.
  t = run(sim, 30 * 5 + 2);
  CHECK(t.restarted == 1);
  CHECK(w.slots[20].overheat == 0.0f);
  CHECK(w.slots[20].heat < 0.3f);  // heating again from 0 since the restart
  CHECK(w.slots[20].type == kDrill);

  // Over a long stretch the four drills take turns: uptime, never damage.
  t = run(sim, 30 * 120);
  CHECK(t.overheated >= 5);
  CHECK(t.damaging == 0);
  CHECK(w.planetHp == hpBefore);
  int standing = 0;
  for (const BuildingSlot& b : w.slots) standing += b.type == kDrill ? 1 : 0;
  CHECK(standing == 4);
}

TEST_CASE("cooler: lowers instability, more on the night side, and stops the overheats") {
  const ContentDB db = drillContent();
  SUBCASE("dark cooler") {
    Sim sim(sandboxConfig(db));
    World& w = sim.world();
    for (const int slot : {20, 21, 22, 23}) place(sim, slot, kDrill);
    place(sim, 18, kCooler);  // night side: full 0.4
    CHECK(w.cooling == doctest::Approx(0.4f));
    CHECK(w.instability == doctest::Approx(0.8f));  // 1.2 - 0.4
    const Tally t = run(sim, 30 * 120);
    CHECK(t.overheated == 0);
    CHECK(drillsRunning(w) == 4);
  }
  SUBCASE("lit cooler: litCoolingMult 0.5") {
    Sim sim(sandboxConfig(db));
    World& w = sim.world();
    for (const int slot : {20, 21, 22, 23}) place(sim, slot, kDrill);
    place(sim, 0, kCooler);  // in the beam
    CHECK(w.cooling == doctest::Approx(0.2f));
    CHECK(w.instability == doctest::Approx(1.0f));  // at the threshold, not above: no heat
    const Tally t = run(sim, 30 * 60);
    CHECK(t.overheated == 0);
  }
  SUBCASE("turning the planet moves the cooler into the light and back") {
    Sim sim(sandboxConfig(db));
    World& w = sim.world();
    place(sim, 18, kCooler);
    CHECK(coolerCooling(w, 18) == doctest::Approx(0.4f));
    w.planetRotation = kPi;  // slot 18 now faces the sun
    sim.step();
    CHECK(coolerCooling(w, 18) == doctest::Approx(0.2f));
  }
}

TEST_CASE("drill and cooler upgrades: income, instability weight and cooling are data") {
  const ContentDB db = drillContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  for (const int slot : {20, 21, 22, 23}) place(sim, slot, kDrill);
  sim.step();
  CHECK(w.instability == doctest::Approx(1.2f));
  // Stable Core halves one drill's weight: load 3.5 -> curve 1.0, no longer above.
  w.stardust = 1000.0f;
  sim.push(cmd::upgradeBuilding(sim.tick(), 20, 1));
  sim.step();
  CHECK(w.slots[20].tiers[1] == 1);
  CHECK(w.instabilityLoad == doctest::Approx(3.5f));
  CHECK(w.instability == doctest::Approx(1.0f));
  // Deep Bore adds income on another drill.
  sim.push(cmd::upgradeBuilding(sim.tick(), 21, 0));
  sim.step();
  CHECK(w.drillIncomePerSecond == doctest::Approx(4.0f * kDrillRate + 0.5f));
  // Radiators raise a cooler's cooling.
  place(sim, 18, kCooler);
  w.stardust = 1000.0f;
  sim.push(cmd::upgradeBuilding(sim.tick(), 18, 0));
  sim.step();
  CHECK(w.cooling == doctest::Approx(0.6f));
  const BuildingDef& cooler = db.buildings[kCooler];
  const UpgradePreview pv = upgradePreview(db.rules, cooler, w.slots[18].tiers, 0, 1000.0f);
  CHECK_FALSE(pv.hasNext);  // one-tier path in the tiny content
}

TEST_CASE("drill: an overheat run replays to the same hash") {
  const ContentDB db = drillContent();
  uint64_t hashes[2] = {0, 0};
  for (uint64_t& h : hashes) {
    Sim sim(sandboxConfig(db));
    for (const int slot : {20, 21, 22, 23, 24}) place(sim, slot, kDrill);
    run(sim, 30 * 40);
    h = sim.hash();
  }
  CHECK(hashes[0] == hashes[1]);
}
