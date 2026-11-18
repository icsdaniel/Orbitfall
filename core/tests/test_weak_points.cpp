// M2.4 weak points on tiny worlds (PLAN §5.5, DECISIONS Q1): the bearing
// window, body ~25% / weak 3-4x with the 0.25 floor, the weak-point stat and
// event, the aim hint, and the acceptance scenario: a never-rotating bot kills
// Bulwarks at under 25% of the rate of a bot that rotates to the weak point.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "tiny_content.h"
#include "weak_point_bot.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

struct WeakWorld {
  ContentDB db;
  int lance = kNoIndex;
  int bulwark = kNoIndex;
};

// tinyContent() plus a Lance (±6°, Pierce, precision) and a Bulwark
// (24 HP, weak point 3.5x, body 0.25x, window 8° from the rules).
WeakWorld weakContent() {
  WeakWorld w;
  ContentDB& db = w.db;
  db = tinyContent();
  db.rules.weakWindow = 8.0f * kDeg;
  db.planets[0].hp = 100000.0f;  // scenarios measure kills, not survival

  BuildingDef lance = db.buildings[kGatling];
  lance.id = "lance";
  lance.cost = 45.0f;
  lance.aimMount = false;
  lance.arc = 6.0f * kDeg;
  lance.range = 420.0f;
  lance.fireRate = 1.0f;
  lance.precision = true;
  lance.projectile = {ProjectileKind::Dot, 1400.0f, 2.0f, DamageType::Pierce, 0.0f};
  db.buildings.push_back(lance);
  w.lance = static_cast<int>(db.buildings.size()) - 1;
  db.buildingIds[lance.id] = w.lance;

  EnemyDef b = db.enemies[kRed];
  b.id = "bulwark";
  b.shape = Shape::Square;
  b.hp = b.hpPerLayer = 24.0f;
  b.speed = 24.0f;
  b.size = 13.0f;
  b.flatLandDamage = true;
  b.landDamage = 6.0f;
  b.bounty = 3.0f;
  b.traits = kTraitBulwark;
  b.hasWeakPoint = true;
  b.weakPoint.window = db.rules.weakWindow;
  b.weakPoint.mult = 3.5f;
  b.weakPoint.bodyMult = 0.25f;
  db.enemies.push_back(b);
  w.bulwark = static_cast<int>(db.enemies.size()) - 1;
  db.enemyIds[b.id] = w.bulwark;
  return w;
}

uint32_t putEnemy(World& w, int type, float bearingDeg, float dist) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = dist * std::cos(bearingDeg * kDeg);
  w.enemies.posY[i] = dist * std::sin(bearingDeg * kDeg);
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

// Fires one shot from `slot` at a parked enemy and returns the HP it lost.
float oneShot(WeakWorld& ww, int building, int slot, float bearingDeg, float dist = 250.0f) {
  Sim sim(sandboxConfig(ww.db));
  World& w = sim.world();
  w.stardust = 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
  sim.step();
  const uint32_t i = putEnemy(w, ww.bulwark, bearingDeg, dist);
  const float before = w.enemies.hp[i];
  for (int t = 0; t < 60 && w.projectiles.count == 0; ++t) sim.step();  // first shot out
  REQUIRE(w.projectiles.count == 1);
  for (int t = 0; t < 60 && w.projectiles.count > 0; ++t) sim.step();   // ... and in
  return before - w.enemies.hp[i];
}

}  // namespace

TEST_CASE("weak point: a hit is weak only within the window around the enemy's bearing") {
  const WeakWorld ww = weakContent();
  const EnemyDef& b = ww.db.enemies[static_cast<size_t>(ww.bulwark)];
  const float r = 300.0f;
  auto at = [&](float towerDeg, float enemyDeg) {
    return isWeakPointHit(b, towerDeg * kDeg, r * std::cos(enemyDeg * kDeg), r * std::sin(enemyDeg * kDeg));
  };
  CHECK(at(30.0f, 30.0f));
  CHECK(at(37.9f, 30.0f));
  CHECK(at(22.1f, 30.0f));
  CHECK_FALSE(at(38.5f, 30.0f));
  CHECK_FALSE(at(21.5f, 30.0f));
  // Wraps across 0 / 360.
  CHECK(at(356.0f, 3.0f));
  CHECK(at(-4.0f, 3.0f));
  CHECK_FALSE(at(180.0f, 3.0f));
  // Enemies without a weak point never get weak hits.
  const EnemyDef& red = ww.db.enemies[kRed];
  CHECK_FALSE(isWeakPointHit(red, 0.0f, r, 0.0f));
  CHECK(weakPointFactor(red, true) == 1.0f);
  CHECK(weakPointFactor(red, false) == 1.0f);
}

TEST_CASE("weak point: body about 25%, weak side 3 to 4x, never 0") {
  WeakWorld ww = weakContent();
  const ContentDB& db = ww.db;
  const EnemyDef& b = db.enemies[static_cast<size_t>(ww.bulwark)];
  const BuildingDef& lance = db.buildings[static_cast<size_t>(ww.lance)];
  const float body = hitMultiplier(db, lance, b, 0.5f, false);
  const float weak = hitMultiplier(db, lance, b, 0.5f, true);
  CHECK(body == doctest::Approx(0.25f));
  CHECK(weak >= 3.0f);
  CHECK(weak <= 4.0f);

  // Stacked resistances on a body hit still never go under the floor.
  ContentDB harsh = db;
  harsh.damage.traits[1].mult[static_cast<int>(DamageType::Kinetic)] = 0.5f;  // armored row
  EnemyDef armored = b;
  armored.traits |= kTraitArmored;
  for (int t = 0; t < kDamageTypeCount; ++t) {
    BuildingDef src = lance;
    src.projectile.type = static_cast<DamageType>(t);
    src.falloff = {{0.0f, 0.25f}, {1.0f, 0.25f}};
    CHECK(hitMultiplier(harsh, src, armored, 0.5f, false) >= 0.25f);
    CHECK(hitMultiplier(harsh, src, armored, 0.5f, true) > 0.25f);
  }

  // In a tiny world: a Lance under the Bulwark deals 2 x 3.5 = 7 a shot; a
  // Gatling 20° off deals 1 x 0.25.
  CHECK(oneShot(ww, ww.lance, 0, 0.0f) == doctest::Approx(7.0f));
  CHECK(oneShot(ww, ww.lance, 0, 3.0f, 300.0f) == doctest::Approx(7.0f));
  CHECK(oneShot(ww, kGatling, 2, 0.0f) == doctest::Approx(0.25f));
  // A Gatling right under it gets the weak point too (the rule is geometric).
  CHECK(oneShot(ww, kGatling, 0, 0.0f) == doctest::Approx(3.5f));
}

TEST_CASE("weak point: the window comes from rules unless the enemy overrides it") {
  WeakWorld ww = weakContent();
  EnemyDef& b = ww.db.enemies[static_cast<size_t>(ww.bulwark)];
  b.weakPoint.window = 2.0f * kDeg;
  // 3° off: weak with an 8° window, body with 2°.
  CHECK(oneShot(ww, ww.lance, 0, 3.0f, 300.0f) == doctest::Approx(0.5f));
}

TEST_CASE("weak point: hits are counted and weak hits emit WeakPointHit") {
  WeakWorld ww = weakContent();
  Sim sim(sandboxConfig(ww.db));
  World& w = sim.world();
  w.stardust = 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), 0, ww.lance, 0.0f));
  sim.push(cmd::placeBuilding(sim.tick(), 2, kGatling, 0.0f));
  sim.step();
  putEnemy(w, ww.bulwark, 0.0f, 250.0f);
  uint32_t weakEvents = 0;
  for (int t = 0; t < 90; ++t) {
    w.events.clear();
    sim.step();
    for (const Event& ev : w.events.items) {
      if (ev.type != EventType::WeakPointHit) continue;
      ++weakEvents;
      CHECK(ev.a == ww.bulwark);
      CHECK(ev.b == ww.lance);
      CHECK(ev.value == doctest::Approx(7.0f));
    }
  }
  CHECK(w.weakPointHits > 0);
  CHECK(w.bodyHits > 0);  // the Gatling, 20° off
  CHECK(weakEvents == w.weakPointHits);

  // Drifters (no weak point) count neither.
  Sim plain(sandboxConfig(ww.db));
  plain.world().stardust = 1000.0f;
  plain.push(cmd::placeBuilding(plain.tick(), 0, kGatling, 0.0f));
  plain.step();
  putEnemy(plain.world(), kGreen, 0.0f, 250.0f);
  for (int t = 0; t < 60; ++t) plain.step();
  CHECK(plain.world().weakPointHits == 0);
  CHECK(plain.world().bodyHits == 0);
}

TEST_CASE("weak point hint: the turn that puts the nearest precision tower under the nearest Bulwark") {
  WeakWorld ww = weakContent();
  Sim sim(sandboxConfig(ww.db));
  World& w = sim.world();
  w.stardust = 1000.0f;
  WeakPointHint hint;
  CHECK_FALSE(weakPointHint(w, hint));  // nothing on screen
  sim.push(cmd::placeBuilding(sim.tick(), 0, ww.lance, 0.0f));   // 0°
  sim.push(cmd::placeBuilding(sim.tick(), 9, ww.lance, 0.0f));   // 90°
  sim.push(cmd::placeBuilding(sim.tick(), 18, kGatling, 0.0f));  // 180°, not precision
  sim.step();
  putEnemy(w, kGreen, 175.0f, 150.0f);  // no weak point: ignored
  CHECK_FALSE(weakPointHint(w, hint));
  putEnemy(w, ww.bulwark, 200.0f, 500.0f);
  const uint32_t nearer = putEnemy(w, ww.bulwark, 60.0f, 300.0f);
  REQUIRE(weakPointHint(w, hint));
  CHECK(hint.enemy == nearer);
  CHECK(hint.slot == 9);  // 90° is 30° from 60°; 0° is 60° away
  CHECK(hint.turn == doctest::Approx(-30.0f * kDeg).epsilon(0.001));
  CHECK(hint.bearing == doctest::Approx(60.0f * kDeg).epsilon(0.001));
  CHECK(hint.distance == doctest::Approx(300.0f));
  // Turning the planet by the hint lines it up.
  w.planetRotation = wrapTau(w.planetRotation + hint.turn);
  REQUIRE(weakPointHint(w, hint));
  CHECK(std::fabs(hint.turn) < 0.001f);
}

// The acceptance scenario (PLAN M2.4), on the shipped content (bulwark.json,
// lance.json, gatling.json, the medium planet). Both bots build the same
// thing, what a non-rotating player would: a Lance right under lane A (slot 3
// = 30°) and a Gatling under lane B (slot 17 = 170°). A Bulwark comes down lane
// A every 6 s for 90 s, spread by its spawnJitterDeg. One bot never touches the
// planet; the other uses the rotate-to-weak-point controller. Deterministic
// (seeded jitter); the counts are printed for the balance log.
TEST_CASE("weak point acceptance: a never-rotating bot kills Bulwarks at under 25% of a rotating bot's rate") {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
  const int bulwark = db.enemy_index("bulwark");
  const int lance = db.building_index("lance");
  const int gatling = db.building_index("gatling");
  REQUIRE(bulwark != kNoIndex);
  REQUIRE(lance != kNoIndex);
  REQUIRE(gatling != kNoIndex);
  const int laneA = db.planets[0].lane_index("A");
  REQUIRE(laneA != kNoIndex);
  REQUIRE(db.planets[0].lanes[static_cast<size_t>(laneA)].bearing == doctest::Approx(30.0f * kDeg));

  struct Outcome {
    uint32_t kills = 0, spawned = 0, weakHits = 0, bodyHits = 0;
    uint64_t hash = 0;
  };
  auto run = [&](bool rotate, uint32_t seed) {
    SimConfig cfg = sandboxConfig(db, seed);
    cfg.planet = 0;
    cfg.sun = 0;
    Sim sim(cfg);
    World& w = sim.world();
    w.stardust = 1000.0f;
    sim.push(cmd::placeBuilding(sim.tick(), 3, lance, 0.0f));
    sim.push(cmd::placeBuilding(sim.tick(), 17, gatling, 0.0f));
    sim.step();
    w.planetHp = w.planetHpMax = 1.0e6f;  // measures kills, not survival
    orbitfall_bots::WeakPointAimer aimer;
    Outcome o;
    for (int t = 0; t < 120 * 30; ++t) {
      if (t < 90 * 30 && t % (6 * 30) == 0 && spawnEnemy(w, bulwark, laneA) != kInvalidIndex) ++o.spawned;
      if (rotate) sim.push(cmd::rotateInput(sim.tick(), aimer.input(w)));
      w.events.clear();
      sim.step();
      for (const Event& ev : w.events.items)
        if (ev.type == EventType::EnemyKilled && ev.a == bulwark) ++o.kills;
    }
    o.weakHits = w.weakPointHits;
    o.bodyHits = w.bodyHits;
    o.hash = sim.hash();
    return o;
  };

  uint32_t stillKills = 0, turningKills = 0;
  for (uint32_t seed = 1; seed <= 4; ++seed) {
    const Outcome still = run(false, seed);
    const Outcome turning = run(true, seed);
    const Outcome again = run(true, seed);
    MESSAGE("seed " << seed << ": " << still.spawned << " Bulwarks; never-rotate kills " << still.kills
                    << " (weak/body hits " << still.weakHits << "/" << still.bodyHits
                    << "), rotate-to-weak-point kills " << turning.kills << " ("
                    << turning.weakHits << "/" << turning.bodyHits << ")");
    CHECK(again.hash == turning.hash);  // deterministic
    CHECK(turning.kills >= still.spawned * 8 / 10);
    stillKills += still.kills;
    turningKills += turning.kills;
  }
  CHECK(turningKills > 0);
  CHECK(static_cast<float>(stillKills) < 0.25f * static_cast<float>(turningKills));
}
