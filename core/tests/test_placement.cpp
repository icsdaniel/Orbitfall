// M2.1 acceptance: placement depth on tiny worlds. Elevated ring with
// foundations and a range bonus, 1 to 3 slot footprints, and the adjacency
// framework (auras applied when the layout changes, within k slots).
#include <vector>

#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/buildings.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

constexpr int kRing = 36;  // slots per ring of the tiny planet
int elevatedSite(int slot) { return kRing + slot; }

// Test-only buildings appended to tinyContent (indices below).
enum PlacementBuilding {
  kAmplifier = 3,  // support, +20% range to towers within 2 slots
  kWide = 4,       // 3-slot panel
  kDuo = 5,        // 2-slot gatling, any layer
  kBooster = 6,    // support: +50% fire rate and +100% damage to towers, +50% income to panels, k = 1
  kPerch = 7,      // elevated-only gatling
};

ContentDB placementContent() {
  ContentDB db = tinyContent();
  db.buildings[kGatling].layer = BuildingLayer::Any;
  db.buildings[kCannon].layer = BuildingLayer::Any;
  const uint32_t towers = 1u << static_cast<uint32_t>(BuildingKind::Tower);
  const uint32_t panels = 1u << static_cast<uint32_t>(BuildingKind::Panel);

  BuildingDef amp;
  amp.id = "test_amplifier";
  amp.kind = BuildingKind::Support;
  amp.layer = BuildingLayer::Any;
  amp.cost = 10.0f;
  amp.auras.push_back({AuraStat::Range, 0.2f, 2, towers});
  db.buildings.push_back(amp);

  BuildingDef wide = db.buildings[kPanel];
  wide.id = "test_wide_panel";
  wide.footprint = 3;
  db.buildings.push_back(wide);

  BuildingDef duo = db.buildings[kGatling];
  duo.id = "test_duo";
  duo.footprint = 2;
  db.buildings.push_back(duo);

  BuildingDef boost;
  boost.id = "test_booster";
  boost.kind = BuildingKind::Support;
  boost.cost = 10.0f;
  boost.auras.push_back({AuraStat::FireRate, 0.5f, 1, towers});
  boost.auras.push_back({AuraStat::Damage, 1.0f, 1, towers});
  boost.auras.push_back({AuraStat::Income, 0.5f, 1, panels});
  db.buildings.push_back(boost);

  BuildingDef perch = db.buildings[kGatling];
  perch.id = "test_perch";
  perch.layer = BuildingLayer::Elevated;
  db.buildings.push_back(perch);

  for (size_t i = 0; i < db.buildings.size(); ++i) db.buildingIds[db.buildings[i].id] = static_cast<int>(i);
  return db;
}

std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

// Places through a command with plenty of stardust; returns true when applied.
bool place(Sim& sim, int site, int building) {
  sim.world().stardust = 10000.0f;
  sim.world().events.clear();
  sim.push(cmd::placeBuilding(sim.tick(), site, building, 0.0f));
  sim.step();
  return eventsOf(sim, EventType::BuildingPlaced).size() == 1;
}

bool sell(Sim& sim, int site) {
  sim.world().events.clear();
  sim.push(cmd::sellBuilding(sim.tick(), site));
  sim.step();
  return eventsOf(sim, EventType::BuildingSold).size() == 1;
}

int lastReject(const Sim& sim) {
  const auto r = eventsOf(sim, EventType::CommandRejected);
  return r.empty() ? 0 : r.back().b;
}

uint32_t putEnemy(World& w, int type, float x, float y) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

}  // namespace

TEST_CASE("placement: the planet has an elevated ring unless its definition turns it off") {
  ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  CHECK(sim.world().slotCount() == 36);
  CHECK(sim.world().siteCount() == 72);
  CHECK(sim.world().siteElevated(36));
  CHECK_FALSE(sim.world().siteElevated(35));
  CHECK(sim.world().siteAngle(36 + 9) == sim.world().slotAngle(9));

  db.planets[0].elevatedLayer = false;
  Sim flat(sandboxConfig(db));
  CHECK(flat.world().siteCount() == 36);
  CHECK_FALSE(place(flat, 36, kGatling));
  CHECK(lastReject(flat) == static_cast<int32_t>(RejectReason::InvalidSlot));
}

TEST_CASE("placement: an elevated slot needs a foundation on the ground slot below") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  CHECK_FALSE(place(sim, elevatedSite(4), kGatling));
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::NoFoundation));
  CHECK(sim.world().stardust == 10000.0f);  // not charged
  CHECK(sim.world().elevated[4].type == -1);

  REQUIRE(place(sim, 4, kPanel));  // any building is a foundation
  REQUIRE(place(sim, elevatedSite(4), kGatling));
  const World& w = sim.world();
  CHECK(w.elevated[4].type == kGatling);
  CHECK(w.slots[4].type == kPanel);
  const auto placed = eventsOf(sim, EventType::BuildingPlaced);
  CHECK(placed[0].a == elevatedSite(4));
  CHECK(w.stardust == doctest::Approx(10000.0f - 50.0f).epsilon(0.0001));  // + a tick of panel income

  // The neighbour column still has no foundation; the slot itself is now taken.
  CHECK_FALSE(place(sim, elevatedSite(5), kGatling));
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::NoFoundation));
  CHECK_FALSE(place(sim, elevatedSite(4), kCannon));
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::Occupied));
}

TEST_CASE("placement: a building's layer field says where it may stand") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 0, kGatling));
  CHECK_FALSE(place(sim, elevatedSite(0), kPanel));  // ground only
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::WrongLayer));
  CHECK_FALSE(place(sim, 1, kPerch));  // elevated only
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::WrongLayer));
  CHECK(place(sim, elevatedSite(0), kPerch));
}

TEST_CASE("placement: selling a foundation that carries an elevated building is rejected") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 7, kCannon));
  REQUIRE(place(sim, elevatedSite(7), kGatling));
  const float before = sim.world().stardust;
  CHECK_FALSE(sell(sim, 7));
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::SupportsElevated));
  CHECK(sim.world().slots[7].type == kCannon);
  CHECK(sim.world().stardust == before);

  // Top first, then the foundation.
  CHECK(sell(sim, elevatedSite(7)));
  CHECK(sim.world().elevated[7].type == -1);
  CHECK(sim.world().elevated[7].anchor == -1);
  CHECK(sell(sim, 7));
  CHECK(sim.world().slots[7].type == -1);
}

TEST_CASE("placement: elevated towers get the range bonus from the rules (+15%)") {
  ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 0, kPanel));
  REQUIRE(place(sim, elevatedSite(0), kGatling));  // range 350 -> 402.5
  REQUIRE(place(sim, 18, kGatling));               // ground, opposite side
  World& w = sim.world();
  CHECK(w.elevated[0].rangeMult == doctest::Approx(1.15f));
  CHECK(w.slots[18].rangeMult == 1.0f);

  // The elevated tower stands elevatedHeight (10) above the surface.
  const TowerAim aim = towerAim(w, static_cast<uint32_t>(elevatedSite(0)));
  CHECK(aim.x == doctest::Approx(110.0f));
  CHECK(aim.y == doctest::Approx(0.0f).epsilon(0.001));

  // 380 out: beyond 350, inside 402.5. Only the elevated tower can reach it.
  putEnemy(w, kGreen, 110.0f + 380.0f, 0.0f);
  putEnemy(w, kGreen, -100.0f - 380.0f, 0.0f);
  sim.step();
  CHECK((w.elevated[0].flags & kSlotHasTarget) != 0);
  CHECK((w.slots[18].flags & kSlotHasTarget) == 0);
  // Its shot lives long enough to fly the longer range.
  REQUIRE(w.projectiles.count == 1);
  CHECK(w.projectiles.range[0] == doctest::Approx(402.5f));
  for (int t = 0; t < 30; ++t) sim.step();
  CHECK(w.enemies.count == 1);  // the near one was shot down, the far one was never reached
  CHECK(w.enemies.posX[0] < 0.0f);
  // 410 out is beyond the bonus too.
  putEnemy(w, kGreen, 110.0f + 410.0f, 0.0f);
  sim.step();
  CHECK((w.elevated[0].flags & kSlotHasTarget) == 0);

  // The bonus is data.
  db.rules.elevatedRangeBonus = 0.5f;
  Sim other(sandboxConfig(db));
  REQUIRE(place(other, 0, kPanel));
  REQUIRE(place(other, elevatedSite(0), kGatling));
  CHECK(other.world().elevated[0].rangeMult == doctest::Approx(1.5f));
}

TEST_CASE("footprints: a wide building covers its slots and blocks them") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 10, kWide));  // covers 10, 11, 12
  const World& w = sim.world();
  CHECK(w.slots[10].type == kWide);
  CHECK(w.slots[10].width == 3);
  CHECK(w.slots[11].type == -1);
  CHECK(w.slots[12].type == -1);
  for (int s = 10; s <= 12; ++s) CHECK(w.slots[static_cast<size_t>(s)].anchor == 10);
  CHECK(w.slots[13].anchor == -1);
  // It stands at the centre of its footprint.
  CHECK(w.siteAngle(10) == doctest::Approx(w.slotAngle(11)));

  // Every covered slot is taken, for 1-slot and for overlapping wide buildings.
  for (int s = 10; s <= 12; ++s) {
    CHECK_FALSE(place(sim, s, kGatling));
    CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::Occupied));
  }
  CHECK_FALSE(place(sim, 8, kWide));  // 8, 9, 10 overlaps
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::Occupied));
  CHECK_FALSE(place(sim, 12, kDuo));
  CHECK(place(sim, 13, kDuo));  // 13, 14
  CHECK(place(sim, 7, kWide));  // 7, 8, 9 fits exactly

  // Selling through any covered slot frees all of them, once.
  CHECK(sell(sim, 12));
  CHECK(eventsOf(sim, EventType::BuildingSold)[0].a == 10);
  for (int s = 10; s <= 12; ++s) CHECK(sim.world().slots[static_cast<size_t>(s)].anchor == -1);
  CHECK(place(sim, 11, kGatling));
}

TEST_CASE("footprints: a footprint wraps around the end of the ring") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 35, kWide));  // 35, 0, 1
  const World& w = sim.world();
  CHECK(w.slots[35].anchor == 35);
  CHECK(w.slots[0].anchor == 35);
  CHECK(w.slots[1].anchor == 35);
  CHECK(w.elevated[0].anchor == -1);  // it wrapped inside its own ring
  CHECK_FALSE(place(sim, 0, kGatling));
}

TEST_CASE("footprints: a wide elevated building needs a foundation under every slot") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 20, kGatling));
  CHECK_FALSE(place(sim, elevatedSite(20), kDuo));  // 21 has no foundation
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::NoFoundation));
  REQUIRE(place(sim, 21, kPanel));
  REQUIRE(place(sim, elevatedSite(20), kDuo));
  // Both foundations are now held, whichever one the elevated building covers.
  CHECK_FALSE(sell(sim, 20));
  CHECK_FALSE(sell(sim, 21));
  CHECK(lastReject(sim) == static_cast<int32_t>(RejectReason::SupportsElevated));
  // A wide foundation carries elevated buildings on any of its slots.
  REQUIRE(place(sim, 0, kWide));
  REQUIRE(place(sim, elevatedSite(2), kGatling));
  CHECK_FALSE(sell(sim, 0));
  CHECK(sell(sim, elevatedSite(2)));
  CHECK(sell(sim, 1));
}

TEST_CASE("adjacency: an aura buffs matching buildings within k slots, not beyond") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 8, kGatling));   // 2 away
  REQUIRE(place(sim, 13, kGatling));  // 3 away
  REQUIRE(place(sim, 9, kPanel));     // in reach, wrong kind
  CHECK(sim.world().slots[8].rangeMult == 1.0f);
  REQUIRE(place(sim, 10, kAmplifier));  // +20% range to towers, k = 2
  const World& w = sim.world();
  CHECK(w.slots[8].rangeMult == doctest::Approx(1.2f));
  CHECK(w.slots[13].rangeMult == 1.0f);
  CHECK(w.slots[9].rangeMult == 1.0f);
  CHECK(w.slots[10].rangeMult == 1.0f);  // never itself
  REQUIRE(w.auraLinks.size() == 1);
  CHECK(w.auraLinks[0].source == 10);
  CHECK(w.auraLinks[0].target == 8);
  CHECK(w.auraLinks[0].stat == static_cast<uint8_t>(AuraStat::Range));

  // A later neighbour is picked up when it is placed (layout change).
  REQUIRE(place(sim, 12, kCannon));
  CHECK(sim.world().slots[12].rangeMult == doctest::Approx(1.2f));
  CHECK(sim.world().auraLinks.size() == 2);

  // The buff is real: 400 out is beyond 350 and inside 420.
  World& world = sim.world();
  const TowerAim aim = towerAim(world, 8);
  putEnemy(world, kGreen, aim.x + aim.ax * 400.0f, aim.y + aim.ay * 400.0f);
  sim.step();
  CHECK((world.slots[8].flags & kSlotHasTarget) != 0);

  // Selling the source removes the buff at once.
  REQUIRE(sell(sim, 10));
  CHECK(sim.world().slots[8].rangeMult == 1.0f);
  CHECK(sim.world().slots[12].rangeMult == 1.0f);
  CHECK(sim.world().auraLinks.empty());
  sim.step();
  CHECK((sim.world().slots[8].flags & kSlotHasTarget) == 0);
}

TEST_CASE("adjacency: auras add up, wrap around the ring, reach the other layer and stack with it") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 0, kGatling));
  REQUIRE(place(sim, 34, kAmplifier));  // 2 away across the seam
  REQUIRE(place(sim, 2, kAmplifier));
  CHECK(sim.world().slots[0].rangeMult == doctest::Approx(1.4f));  // 1 + 0.2 + 0.2

  // Above the gatling: distance 0 from both... 2 from both amplifiers.
  REQUIRE(place(sim, elevatedSite(0), kCannon));
  CHECK(sim.world().elevated[0].rangeMult == doctest::Approx(1.4f * 1.15f));
  // An elevated amplifier reaches the ground ring too.
  REQUIRE(place(sim, elevatedSite(2), kAmplifier));
  CHECK(sim.world().slots[0].rangeMult == doctest::Approx(1.6f));
}

TEST_CASE("adjacency: distance is measured from the edge of a footprint") {
  const ContentDB db = placementContent();
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 10, kDuo));        // 10, 11
  REQUIRE(place(sim, 13, kAmplifier));  // 2 from slot 11, 3 from the anchor
  CHECK(sim.world().slots[10].rangeMult == doctest::Approx(1.2f));
  REQUIRE(place(sim, 20, kDuo));        // 20, 21
  REQUIRE(place(sim, 24, kAmplifier));  // 3 from slot 21
  CHECK(sim.world().slots[20].rangeMult == 1.0f);
}

TEST_CASE("adjacency: fire rate, damage and income auras change what the buildings do") {
  const ContentDB db = placementContent();

  // Shots in 3 seconds with and without +50% fire rate.
  const auto shots = [&](bool boosted, float& damage) {
    Sim sim(sandboxConfig(db));
    REQUIRE(place(sim, 0, kGatling));
    if (boosted) REQUIRE(place(sim, 1, kBooster));
    World& w = sim.world();
    const uint32_t e = putEnemy(w, kEclipser, 400.0f, 0.0f);  // 40 hp, in arc
    w.enemies.flags[e] = 0;
    w.events.clear();
    uint32_t fired = 0, last = 0;
    for (int t = 0; t < 90; ++t) {
      w.enemies.posX[e] = 400.0f;  // hold it still
      w.enemies.posY[e] = 0.0f;
      w.enemies.hp[e] = 1000.0f;
      sim.step();
      if (w.projectiles.count > last) fired += w.projectiles.count - last;
      last = w.projectiles.count;
      if (w.projectiles.count > 0) damage = w.projectiles.damage[0];
    }
    return fired;
  };
  float plainDamage = 0.0f, boostedDamage = 0.0f;
  const uint32_t plain = shots(false, plainDamage);
  const uint32_t boosted = shots(true, boostedDamage);
  CHECK(plain >= 5);
  CHECK(static_cast<float>(boosted) / static_cast<float>(plain) == doctest::Approx(1.5f).epsilon(0.25));
  CHECK(plainDamage == 1.0f);
  CHECK(boostedDamage == 2.0f);

  // A lit panel next to the booster earns 50% more.
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 0, kPanel));  // the sun starts at bearing 0
  sim.step();
  const float base = sim.world().incomePerSecond;
  REQUIRE(base > 0.0f);
  REQUIRE(place(sim, 35, kBooster));
  sim.step();
  CHECK(sim.world().incomePerSecond == doctest::Approx(base * 1.5f));
  CHECK(sim.world().slots[0].rangeMult == 1.0f);  // tower stats are not for panels
}

TEST_CASE("adjacency: multipliers are recomputed on layout changes only, and a stack never drops below the floor") {
  ContentDB db = placementContent();
  db.buildings[kAmplifier].auras[0].amount = -0.6f;
  Sim sim(sandboxConfig(db));
  REQUIRE(place(sim, 5, kGatling));
  REQUIRE(place(sim, 4, kAmplifier));
  REQUIRE(place(sim, 6, kAmplifier));
  CHECK(sim.world().slots[5].rangeMult == kMinAuraMult);  // 1 - 1.2 clamps

  // Ticks do not touch the multipliers: a poked value stays until the next change.
  sim.world().slots[5].rangeMult = 7.0f;
  for (int t = 0; t < 10; ++t) sim.step();
  CHECK(sim.world().slots[5].rangeMult == 7.0f);
  REQUIRE(place(sim, 20, kPanel));
  CHECK(sim.world().slots[5].rangeMult == kMinAuraMult);
}

TEST_CASE("placement: elevated and wide layouts replay to the same hash") {
  const ContentDB db = placementContent();
  const auto run = [&]() {
    Sim sim(sandboxConfig(db, 7));
    sim.world().stardust = 5000.0f;
    sim.push(cmd::placeBuilding(0, 3, kGatling, 0.0f));
    sim.push(cmd::placeBuilding(1, elevatedSite(3), kCannon, 0.2f));
    sim.push(cmd::placeBuilding(2, 4, kAmplifier, 0.0f));
    sim.push(cmd::placeBuilding(3, 10, kWide, 0.0f));
    sim.push(cmd::sellBuilding(40, 3));  // rejected
    sim.push(cmd::sellBuilding(50, 11));
    sim.push(cmd::setTargeting(55, elevatedSite(3), static_cast<uint8_t>(TargetingMode::Strong)));
    sim.push(cmd::setAim(56, elevatedSite(3), -0.3f));
    for (int i = 0; i < 20; ++i) spawnEnemy(sim.world(), kGreen, 0);
    for (int t = 0; t < 300; ++t) sim.step();
    CHECK(sim.world().elevated[3].mode == static_cast<uint8_t>(TargetingMode::Strong));
    CHECK(sim.world().elevated[3].aimOffset == doctest::Approx(-0.3f));
    return sim.hash();
  };
  const uint64_t a = run();
  CHECK(a == run());
  // The elevated layout is part of the state.
  Sim other(sandboxConfig(db, 7));
  other.world().stardust = 5000.0f;
  for (int t = 0; t < 300; ++t) other.step();
  CHECK(other.hash() != a);
}
