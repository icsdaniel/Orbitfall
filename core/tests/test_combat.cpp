// M1.6 acceptance: out-of-arc never targeted; aimOffset honoured; targeting
// modes; projectiles, layer popping, splash, bounty and the damage floor.
#include <cmath>
#include <vector>

#include "alloc_counter.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

// A motionless enemy at a world position.
uint32_t putEnemy(World& w, int type, float x, float y) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

// Sim with one tower on `slot`, free of charge.
void addTower(Sim& sim, int slot, int building, float aimOffset = 0.0f) {
  sim.world().stardust = 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, aimOffset));
  sim.step();
  REQUIRE(sim.world().slots[static_cast<size_t>(slot)].type == building);
}

// Polar position relative to the tower on slot 0 at rotation 0, i.e. (100, 0).
void fromTower(float distance, float angleDeg, float& x, float& y) {
  x = 100.0f + distance * std::cos(angleDeg * kDeg);
  y = distance * std::sin(angleDeg * kDeg);
}

}  // namespace

TEST_CASE("targeting: an enemy is targeted iff it is in range and inside the arc") {
  const ContentDB db = tinyContent();
  Rng rng;
  rng.seed(11);
  int in = 0, out = 0;
  for (int k = 0; k < 400; ++k) {
    Sim sim(sandboxConfig(db));
    addTower(sim, 0, kGatling);  // +-40 deg, range 350
    const float distance = rng.range(20.0f, 500.0f);
    const float angle = rng.range(-180.0f, 180.0f);
    if (std::fabs(std::fabs(angle) - 40.0f) < 0.5f || std::fabs(distance - 350.0f) < 1.0f) continue;
    float x, y;
    fromTower(distance, angle, x, y);
    putEnemy(sim.world(), kGreen, x, y);
    sim.step();
    const bool expected = distance <= 350.0f && std::fabs(angle) <= 40.0f;
    const bool targeted = (sim.world().slots[0].flags & kSlotHasTarget) != 0;
    CHECK(targeted == expected);
    // Out of arc: never shot at. (A shot at a very near enemy lands the same tick.)
    const bool shotAt = sim.world().projectiles.count > 0 || sim.world().enemies.type[0] != kGreen;
    CHECK(shotAt == expected);
    expected ? ++in : ++out;
  }
  CHECK(in > 20);
  CHECK(out > 200);
}

TEST_CASE("targeting: an out-of-arc enemy is never hit however long it sits there") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kCannon);  // +-25 deg
  float x, y;
  fromTower(200.0f, 35.0f, x, y);
  putEnemy(sim.world(), kRed, x, y);
  for (int i = 0; i < 300; ++i) sim.step();
  CHECK(sim.world().enemies.count == 1);
  CHECK(sim.world().projectiles.count == 0);
  CHECK(eventsOf(sim, EventType::EnemyKilled).empty());
}

TEST_CASE("targeting: aimOffset turns the axis") {
  const ContentDB db = tinyContent();
  float x, y;
  // 60 deg off the slot normal: outside a +-40 arc, inside it once the mount is at +30.
  fromTower(200.0f, 60.0f, x, y);
  {
    Sim sim(sandboxConfig(db));
    addTower(sim, 0, kGatling, 0.0f);
    putEnemy(sim.world(), kRed, x, y);
    sim.step();
    CHECK((sim.world().slots[0].flags & kSlotHasTarget) == 0);
  }
  {
    Sim sim(sandboxConfig(db));
    addTower(sim, 0, kGatling, 30.0f * kDeg);
    putEnemy(sim.world(), kRed, x, y);
    sim.step();
    CHECK((sim.world().slots[0].flags & kSlotHasTarget) != 0);
    // ... and what was dead ahead at -20 deg is now 50 deg off the axis.
    Sim other(sandboxConfig(db));
    addTower(other, 0, kGatling, 30.0f * kDeg);
    fromTower(200.0f, -20.0f, x, y);
    putEnemy(other.world(), kRed, x, y);
    other.step();
    CHECK((other.world().slots[0].flags & kSlotHasTarget) == 0);
  }
  {
    // SetAim moves it after placement too.
    Sim sim(sandboxConfig(db));
    addTower(sim, 0, kGatling, 0.0f);
    fromTower(200.0f, 60.0f, x, y);
    putEnemy(sim.world(), kRed, x, y);
    sim.push(cmd::setAim(sim.tick(), 0, 30.0f * kDeg));
    sim.step();
    CHECK((sim.world().slots[0].flags & kSlotHasTarget) != 0);
  }
}

TEST_CASE("targeting: the axis turns with the planet and with the slot") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 9, kGatling);  // slot 9 = 90 deg: faces +y
  World& w = sim.world();
  const uint32_t up = putEnemy(w, kGreen, 0.0f, 300.0f);
  sim.step();
  CHECK(w.slotTargets[9] == up);
  // Quarter turn: the slot now faces -x, the enemy above is out of arc.
  w.planetRotation = kPi / 2.0f;
  sim.step();
  CHECK((w.slots[9].flags & kSlotHasTarget) == 0);
  const uint32_t left = putEnemy(w, kGreen, -300.0f, 0.0f);
  sim.step();
  CHECK(w.slotTargets[9] == left);
  const TowerAim aim = towerAim(w, 9);
  CHECK(aim.x == doctest::Approx(-100.0f));
  CHECK(aim.ax == doctest::Approx(-1.0f));
}

TEST_CASE("targeting: modes first, strong and close pick different enemies") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  World& w = sim.world();
  w.slots[0].cooldown = 1000.0f;  // only look, do not shoot
  // Nearest the planet centre (but off to the side, so not nearest the tower).
  float x, y;
  fromTower(120.0f, 39.0f, x, y);
  const uint32_t first = putEnemy(w, kRed, x, y);
  // Nearest the tower.
  const uint32_t close = putEnemy(w, kBlue, 215.0f, 0.0f);
  // Most hit points, far out.
  const uint32_t strong = putEnemy(w, kGreen, 400.0f, 10.0f);
  REQUIRE(std::hypot(w.enemies.posX[first], w.enemies.posY[first]) < 215.0f);
  REQUIRE(std::hypot(w.enemies.posX[first] - 100.0f, w.enemies.posY[first]) > 115.0f);

  sim.step();
  CHECK(w.slotTargets[0] == first);  // default mode is the first listed
  sim.push(cmd::setTargeting(sim.tick(), 0, static_cast<uint8_t>(TargetingMode::Strong)));
  sim.step();
  CHECK(w.slotTargets[0] == strong);
  sim.push(cmd::setTargeting(sim.tick(), 0, static_cast<uint8_t>(TargetingMode::Close)));
  sim.step();
  CHECK(w.slotTargets[0] == close);
}

TEST_CASE("projectiles: a shot kills a one-layer enemy and pays the bounty") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  World& w = sim.world();
  w.stardust = 0.0f;
  putEnemy(w, kRed, 300.0f, 20.0f);
  sim.step();
  REQUIRE(w.projectiles.count == 1);
  CHECK(std::hypot(w.projectiles.velX[0], w.projectiles.velY[0]) == doctest::Approx(900.0f));
  for (int i = 0; i < 30 && w.enemies.count > 0; ++i) sim.step();
  CHECK(w.enemies.count == 0);
  const auto killed = eventsOf(sim, EventType::EnemyKilled);
  REQUIRE(killed.size() == 1);
  CHECK(killed[0].a == kRed);
  CHECK(killed[0].x == doctest::Approx(300.0f));
  CHECK(killed[0].value == 1.0f);
  CHECK(w.stardust == 1.0f);
  CHECK(w.planetHp == 100.0f);
}

TEST_CASE("projectiles: shots lead a moving target and expire at range") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kCannon);  // slow shell, speed 600
  World& w = sim.world();
  // A dart crossing the arc sideways at 110.
  const uint32_t i = putEnemy(w, kDart, 400.0f, -60.0f);
  w.enemies.velY[i] = 110.0f;
  for (int t = 0; t < 40 && w.enemies.count > 0; ++t) sim.step();
  CHECK(w.enemies.count == 0);
  CHECK(eventsOf(sim, EventType::EnemyKilled).size() == 1);

  // A shot that misses everything is removed once it has flown its range.
  Sim miss(sandboxConfig(db));
  addTower(miss, 0, kGatling);
  const uint32_t k = miss.world().projectiles.add();
  miss.world().projectiles.posX[k] = 100.0f;
  miss.world().projectiles.velX[k] = 900.0f;
  miss.world().projectiles.life[k] = 350.0f / 900.0f;
  miss.world().projectiles.type[k] = kGatling;
  for (int t = 0; t < 11; ++t) miss.step();
  CHECK(miss.world().projectiles.count == 1);
  miss.step();
  CHECK(miss.world().projectiles.count == 0);
  CHECK(miss.world().projectiles.posX[0] <= 100.0f + 350.0f + 30.0f + 1.0f);
}

TEST_CASE("layers: each point of damage pops one layer down the popsInto chain") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = putEnemy(w, kGreen, 300.0f, 0.0f);
  CHECK(damageEnemy(w, i, 1.0f) == false);
  CHECK(w.enemies.type[i] == kBlue);
  CHECK(remainingLayers(db.enemies[w.enemies.type[i]], w.enemies.hp[i]) == 2);
  CHECK(damageEnemy(w, i, 1.0f) == false);
  CHECK(w.enemies.type[i] == kRed);
  CHECK(damageEnemy(w, i, 1.0f) == true);
  CHECK((w.enemies.flags[i] & kEnemyDead) != 0);
  CHECK(damageEnemy(w, i, 1.0f) == false);  // already dead
  const auto popped = eventsOf(sim, EventType::LayerPopped);
  REQUIRE(popped.size() == 2);
  CHECK(popped[0].a == kGreen);
  CHECK(popped[0].b == kBlue);
  CHECK(popped[1].a == kBlue);
  CHECK(popped[1].b == kRed);
  CHECK(eventsOf(sim, EventType::EnemyKilled).size() == 1);
  sim.step();
  CHECK(w.enemies.count == 0);  // cleanup removed it
}

TEST_CASE("layers: extra damage carries through; a popped layer lands for less") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t a = putEnemy(w, kGreen, 300.0f, 0.0f);
  CHECK(damageEnemy(w, a, 2.0f) == false);  // green -> red in one hit
  CHECK(w.enemies.type[a] == kRed);
  const uint32_t b = putEnemy(w, kGreen, 300.0f, 50.0f);
  CHECK(damageEnemy(w, b, 3.0f) == true);
  const uint32_t c = putEnemy(w, kEclipser, 300.0f, 100.0f);
  CHECK(damageEnemy(w, c, 39.0f) == false);  // one layer of 40 hp
  CHECK(w.enemies.hp[c] == doctest::Approx(1.0f));
  CHECK(damageEnemy(w, c, 1.0f) == true);
  CHECK(w.stardust == doctest::Approx(150.0f + 1.0f + 15.0f));

  // The red left over from the green lands for one layer, not three.
  w.enemies.posX[a] = 99.0f;
  sim.step();
  const auto hits = eventsOf(sim, EventType::PlanetHit);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].value == 1.0f);
}

TEST_CASE("layers: a gatling pops a green drifter layer by layer") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  putEnemy(sim.world(), kGreen, 250.0f, 0.0f);
  for (int i = 0; i < 60 && sim.world().enemies.count > 0; ++i) sim.step();
  CHECK(sim.world().enemies.count == 0);
  CHECK(eventsOf(sim, EventType::LayerPopped).size() == 2);
  CHECK(eventsOf(sim, EventType::EnemyKilled).size() == 1);
}

TEST_CASE("splash: a shell damages everything in its radius and nothing outside") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kCannon);  // damage 3, splash 40
  World& w = sim.world();
  w.slots[0].mode = static_cast<uint8_t>(TargetingMode::Close);
  putEnemy(w, kGreen, 300.0f, 0.0f);   // the target: 3 layers, dies
  putEnemy(w, kGreen, 330.0f, 20.0f);  // 36 from the impact: dies
  putEnemy(w, kBlue, 300.0f, -45.0f);  // edge of its disc inside the blast: dies
  putEnemy(w, kGreen, 300.0f, 70.0f);  // 70 away: untouched
  putEnemy(w, kRed, 380.0f, 0.0f);     // behind, 88 away: untouched
  for (int i = 0; i < 20 && w.projectiles.count == 0; ++i) sim.step();
  REQUIRE(w.projectiles.count == 1);
  w.slots[0].cooldown = 1000.0f;  // one shell only
  for (int i = 0; i < 30 && w.projectiles.count > 0; ++i) sim.step();
  CHECK(eventsOf(sim, EventType::EnemyKilled).size() == 3);
  REQUIRE(w.enemies.count == 2);
  for (uint32_t i = 0; i < w.enemies.count; ++i) {
    const bool green = w.enemies.type[i] == kGreen && w.enemies.posY[i] == 70.0f;
    const bool red = w.enemies.type[i] == kRed && w.enemies.posX[i] == 380.0f;
    CHECK((green || red));
    CHECK(w.enemies.hp[i] == 1.0f);
  }
}

TEST_CASE("splash: a dot without splash only hurts the enemy it hits") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  World& w = sim.world();
  w.slots[0].mode = static_cast<uint8_t>(TargetingMode::Close);
  putEnemy(w, kRed, 300.0f, 0.0f);
  putEnemy(w, kRed, 300.0f, 17.0f);
  sim.step();
  w.slots[0].cooldown = 1000.0f;
  for (int i = 0; i < 30 && w.projectiles.count > 0; ++i) sim.step();
  CHECK(w.enemies.count == 1);
  CHECK(w.enemies.posY[0] == 17.0f);
}

TEST_CASE("damage: type multipliers and falloff apply, never below the 0.25 floor") {
  ContentDB db = tinyContent();
  const EnemyDef& red = db.enemies[kRed];
  CHECK(damageMultiplier(db, db.buildings[kGatling], red, 0.5f) == 1.0f);

  db.enemies[kRed].damageMult[static_cast<int>(DamageType::Kinetic)] = 0.5f;
  CHECK(damageMultiplier(db, db.buildings[kGatling], red, 0.5f) == 0.5f);
  CHECK(damageMultiplier(db, db.buildings[kCannon], red, 0.5f) == 1.0f);  // explosive unaffected

  db.buildings[kGatling].falloff = {{0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 0.3f}};
  CHECK(falloffAt(db.buildings[kGatling], 0.25f) == 1.0f);
  CHECK(falloffAt(db.buildings[kGatling], 0.75f) == doctest::Approx(0.65f));
  CHECK(falloffAt(db.buildings[kGatling], 1.0f) == doctest::Approx(0.3f));
  // 0.5 x 0.3 = 0.15 would be a hard counter: floored to 0.25.
  CHECK(damageMultiplier(db, db.buildings[kGatling], red, 1.0f) == 0.25f);
  // A content floor above 0.25 is honoured, one below is not.
  db.rules.minDamageMult = 0.4f;
  CHECK(damageMultiplier(db, db.buildings[kGatling], red, 1.0f) == 0.4f);
  db.rules.minDamageMult = 0.1f;
  CHECK(damageMultiplier(db, db.buildings[kGatling], red, 1.0f) == 0.25f);
}

TEST_CASE("damage: a floored hit still hurts, four gatling hits kill a 1 hp enemy") {
  ContentDB db = tinyContent();
  db.enemies[kRed].damageMult[static_cast<int>(DamageType::Kinetic)] = 0.0f;  // "immune"
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  World& w = sim.world();
  putEnemy(w, kRed, 300.0f, 0.0f);
  uint32_t shots = 0, last = 0;
  for (int i = 0; i < 120 && w.enemies.count > 0; ++i) {
    sim.step();
    if (w.projectiles.count > last) ++shots;
    last = w.projectiles.count;
  }
  CHECK(w.enemies.count == 0);
  CHECK(shots >= 4);
  CHECK(eventsOf(sim, EventType::EnemyKilled).size() == 1);
}

TEST_CASE("fire rate: a gatling fires fireRate shots per second") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  addTower(sim, 0, kGatling);
  World& w = sim.world();
  const uint32_t e = putEnemy(w, kEclipser, 300.0f, 0.0f);
  w.enemies.hp[e] = 1.0e6f;
  for (int i = 0; i < 300; ++i) sim.step();  // 10 s
  // Every shot does 1 damage: hits so far plus the shots still in flight.
  const uint32_t shots = static_cast<uint32_t>(1.0e6f - w.enemies.hp[e] + 0.5f) + w.projectiles.count;
  CHECK(shots >= 58);
  CHECK(shots <= 61);
}

TEST_CASE("combat: a busy tick does not allocate") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  w.stardust = 1.0e6f;
  for (int s = 0; s < 36; ++s) sim.push(cmd::placeBuilding(0, s, s % 2, 0.0f));
  sim.step();
  Rng rng;
  rng.seed(3);
  for (int i = 0; i < 1500; ++i) {
    const float a = rng.range(0.0f, kTau), r = rng.range(150.0f, 500.0f);
    const uint32_t k = putEnemy(w, i % 3, r * std::cos(a), r * std::sin(a));
    w.enemies.velX[k] = -std::cos(a) * 40.0f;
    w.enemies.velY[k] = -std::sin(a) * 40.0f;
  }
  w.events.reserve(1u << 16);
  for (int i = 0; i < 5; ++i) sim.step();
  const uint64_t before = allocationCount();
  for (int i = 0; i < 60; ++i) {
    sim.step();
    w.events.clear();
  }
  CHECK(allocationCount() == before);
  CHECK(w.enemies.count < 1500);
}
