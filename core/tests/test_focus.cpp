// M2.11 acceptance (unit part): axis targeting with bent mounts, the focus-point
// solver, the focus bonus on hits, priced re-aiming between waves and the Sun
// Lens beam merge. Tiny worlds on tinyContent() with the focus rules set here.
#include <cmath>
#include <vector>

#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "systems/focus.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

int kLens = -1;  // index of the beam tower added by focusContent()

ContentDB focusContent() {
  ContentDB db = tinyContent();
  db.rules.focus.bonusPerExtra = 0.25f;
  db.rules.focus.cap = 0.5f;
  db.rules.focus.radius = 50.0f;
  db.rules.focus.minAltitude = 10.0f;
  db.rules.focus.lensMerge = true;
  db.rules.aim.offsets = {-60.0f * kDeg, -30.0f * kDeg, 0.0f, 30.0f * kDeg, 60.0f * kDeg};
  db.rules.aim.reaimCostFrac = 0.1f;
  db.rules.aim.reaimBetweenWavesOnly = true;
  BuildingDef lens = db.buildings[kGatling];
  lens.id = "lens";
  lens.cost = 120.0f;
  lens.arc = 35.0f * kDeg;
  lens.range = 320.0f;
  lens.fireRate = 0.0f;
  lens.projectile = {ProjectileKind::Beam, 0.0f, 4.0f, DamageType::Energy, 0.0f};
  lens.beam.rampSeconds = 0.0f;  // full power at once: easier sums
  lens.beam.channelFloor = 1.0f;
  kLens = static_cast<int>(db.buildings.size());
  db.buildings.push_back(lens);
  db.buildingIds[lens.id] = kLens;
  return db;
}

void place(Sim& sim, int slot, int building, float offsetDeg) {
  sim.world().stardust = 10000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), slot, building, offsetDeg * kDeg));
  sim.step();
  REQUIRE(sim.world().slots[static_cast<size_t>(slot)].type == building);
}

uint32_t putEnemy(World& w, int type, float x, float y) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  w.enemies.velX[i] = w.enemies.velY[i] = 0.0f;
  return i;
}

// Expected crossing radius on the bisector of two towers `delta` rad either
// side of it, leaning `lean` rad toward it (law of sines, planet radius 100).
float crossRadius(float delta, float lean) { return 100.0f * std::sin(kPi - lean) / std::sin(lean - delta); }

std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

}  // namespace

TEST_CASE("M2.11 axis targeting: a bent mount turns the arc by its offset") {
  const ContentDB db = focusContent();
  // Gatling on slot 0 (world 0 deg), arc +-40. An enemy 60 deg off the normal
  // as seen from the tower: out of arc straight, inside with +30 and +60
  // mounts, out again with -30.
  struct Case {
    float offset;
    bool expected;
  };
  for (const Case c : {Case{0.0f, false}, Case{30.0f, true}, Case{60.0f, true}, Case{-30.0f, false}}) {
    Sim sim(sandboxConfig(db));
    place(sim, 0, kGatling, c.offset);
    CHECK(sim.world().slots[0].aimOffset == doctest::Approx(c.offset * kDeg));
    putEnemy(sim.world(), kGreen, 100.0f + 200.0f * std::cos(60.0f * kDeg), 200.0f * std::sin(60.0f * kDeg));
    sim.step();
    CHECK(((sim.world().slots[0].flags & kSlotHasTarget) != 0) == c.expected);
  }
  // The offset follows the planet's rotation like the slot does.
  Sim sim(sandboxConfig(db));
  place(sim, 0, kGatling, -30.0f);
  const TowerAim aim = towerAim(sim.world(), 0);
  CHECK(std::atan2(aim.ay, aim.ax) == doctest::Approx(-30.0f * kDeg).epsilon(1e-4));
}

TEST_CASE("M2.11 mounts snap to the data's steps; placing a mount is free") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  sim.world().stardust = 1000.0f;
  sim.push(cmd::placeBuilding(0, 0, kGatling, 41.0f * kDeg));  // nearest step: 30
  sim.push(cmd::placeBuilding(0, 1, kGatling, 99.0f * kDeg));  // clamped to 60
  sim.push(cmd::placeBuilding(0, 2, kPanel, 30.0f * kDeg));    // no mount
  sim.step();
  CHECK(sim.world().slots[0].aimOffset == doctest::Approx(30.0f * kDeg));
  CHECK(sim.world().slots[1].aimOffset == doctest::Approx(60.0f * kDeg));
  CHECK(sim.world().slots[2].aimOffset == 0.0f);
  CHECK(sim.world().stardust == doctest::Approx(1000.0f - 2 * 50.0f - 40.0f).epsilon(0.001));
}

TEST_CASE("M2.11 focus solver: two leaning towers cross on their bisector") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  // Slots 1 and 5 (10 and 50 deg) lean 30 deg toward 30 deg.
  place(sim, 1, kGatling, 30.0f);
  CHECK(sim.world().focus.empty());  // one axis crosses nothing
  place(sim, 5, kGatling, -30.0f);
  const World& w = sim.world();
  REQUIRE(w.focus.size() == 1);
  const FocusPoint& f = w.focus[0];
  CHECK(std::sqrt(f.x * f.x + f.y * f.y) == doctest::Approx(crossRadius(20.0f * kDeg, 30.0f * kDeg)).epsilon(1e-3));
  CHECK(std::atan2(f.y, f.x) == doctest::Approx(30.0f * kDeg).epsilon(1e-4));
  CHECK(f.feeders == 2);
  CHECK(f.radius == doctest::Approx(50.0f));
  // A straight tower under the point feeds it too (its axis runs through it).
  place(sim, 3, kGatling, 0.0f);
  REQUIRE(sim.world().focus.size() == 1);
  CHECK(sim.world().focus[0].feeders == 3);
  CHECK(focusMultiplier(db.rules.focus, 3) == doctest::Approx(1.5f));
  CHECK(focusMultiplier(db.rules.focus, 2) == doctest::Approx(1.25f));
  CHECK(focusMultiplier(db.rules.focus, 5) == doctest::Approx(1.5f));  // capped
  CHECK(focusMultiplier(db.rules.focus, 1) == 1.0f);
}

TEST_CASE("M2.11 focus solver: no point for diverging, parallel or out-of-range axes") {
  const ContentDB db = focusContent();
  SUBCASE("straight towers diverge") {
    Sim sim(sandboxConfig(db));
    place(sim, 1, kGatling, 0.0f);
    place(sim, 5, kGatling, 0.0f);
    CHECK(sim.world().focus.empty());
  }
  SUBCASE("leaning apart") {
    Sim sim(sandboxConfig(db));
    place(sim, 1, kGatling, -30.0f);
    place(sim, 5, kGatling, 30.0f);
    CHECK(sim.world().focus.empty());
  }
  SUBCASE("parallel axes never meet; low crossings still count") {
    // Slots 2 and 4 leaning 30 deg together cross low (r ~146); slots 0 and 6
    // leaning 30 deg together are parallel (both axes at 30 deg).
    Sim sim(sandboxConfig(db));
    place(sim, 2, kGatling, 30.0f);
    place(sim, 4, kGatling, -30.0f);
    CHECK(crossRadius(10.0f * kDeg, 30.0f * kDeg) < 200.0f);  // sanity: these do cross low
    CHECK(sim.world().focus.size() == 1);
    Sim far(sandboxConfig(db));
    place(far, 0, kGatling, 30.0f);
    place(far, 6, kGatling, -30.0f);  // parallel axes (both at 30 deg)
    CHECK(far.world().focus.empty());
  }
  SUBCASE("beyond range: no point") {
    ContentDB shortRange = db;
    shortRange.buildings[kGatling].range = 150.0f;  // the r ~288 crossing is ~200 away
    Sim sim(sandboxConfig(shortRange));
    place(sim, 1, kGatling, 30.0f);
    place(sim, 5, kGatling, -30.0f);
    CHECK(sim.world().focus.empty());
  }
  SUBCASE("panels and unmounted buildings never feed") {
    Sim sim(sandboxConfig(db));
    place(sim, 1, kGatling, 30.0f);
    place(sim, 5, kPanel, 0.0f);
    CHECK(sim.world().focus.empty());
  }
}

TEST_CASE("M2.11 focus points close together merge; selling a feeder removes the point") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  // Pairs at +-20 deg / 30 lean (r ~288) and +-40 deg / 60 lean (r ~253) both
  // cross on the 30 deg bearing within 50 of each other.
  place(sim, 1, kGatling, 30.0f);
  place(sim, 5, kGatling, -30.0f);
  place(sim, 35, kGatling, 60.0f);
  place(sim, 7, kGatling, -60.0f);
  place(sim, 3, kGatling, 0.0f);
  const World& w = sim.world();
  REQUIRE(!w.focus.empty());
  int best = 0;
  for (const FocusPoint& f : w.focus) best = f.feeders > best ? f.feeders : best;
  CHECK(best == 5);
  sim.push(cmd::sellBuilding(sim.tick(), 5));
  sim.push(cmd::sellBuilding(sim.tick(), 7));
  sim.push(cmd::sellBuilding(sim.tick(), 3));
  sim.step();
  for (const FocusPoint& f : sim.world().focus) CHECK(f.feeders == 2);  // only 1 and 35 are left
}

TEST_CASE("M2.11 focus points turn with the planet") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  place(sim, 1, kGatling, 30.0f);
  place(sim, 5, kGatling, -30.0f);
  World& w = sim.world();
  const FocusPoint local = w.focus[0];
  w.planetRotation = 90.0f * kDeg;
  float x = 0.0f, y = 0.0f;
  focusWorldPos(w, 0, x, y);
  CHECK(std::atan2(y, x) == doctest::Approx(120.0f * kDeg).epsilon(1e-4));
  CHECK(w.focus[0].x == local.x);  // stored in the planet frame
  // Feeder test follows the rotation: inside at the rotated spot, not at the old one.
  CHECK(focusFeedersAt(w, 1, x, y) == 2);
  CHECK(focusFeedersAt(w, 1, local.x, local.y) == 0);
  CHECK(focusFeedersAt(w, 9, x, y) == 0);  // not a feeder
}

TEST_CASE("M2.11 focus bonus: hits inside the zone from feeders deal +25% per extra feeder") {
  const ContentDB db = focusContent();
  // Measures the damage the slot-1 Gatling deals to a sturdy target sitting
  // on the focus point, with and without the second (feeding) tower present
  // but unable to fire (cooldown held high).
  auto dealt = [&](bool partner, float atRadius) {
    ContentDB d = db;
    d.enemies[kEclipser].behavior = EnemyBehavior::Lane;  // a still 40-HP block
    d.enemies[kEclipser].hasHover = false;
    d.enemies[kEclipser].shadowCaster = false;
    Sim sim(sandboxConfig(d));
    place(sim, 1, kGatling, 30.0f);
    if (partner) place(sim, 5, kCannon, -30.0f);  // feeds, but cannot see the target's side
    World& w = sim.world();
    if (partner) {
      REQUIRE(w.focus.size() == 1);
    }
    const uint32_t e = putEnemy(w, kEclipser, atRadius * std::cos(30.0f * kDeg), atRadius * std::sin(30.0f * kDeg));
    const float before = w.enemies.hp[e];
    for (int i = 0; i < 45; ++i) {
      if (partner) w.slots[5].cooldown = 100.0f;
      sim.step();
    }
    return before - w.enemies.hp[e];
  };
  const float r = crossRadius(20.0f * kDeg, 30.0f * kDeg);
  const float alone = dealt(false, r), fed = dealt(true, r);
  REQUIRE(alone > 0.0f);
  CHECK(fed / alone == doctest::Approx(1.25f).epsilon(0.02));
  // The same pair with the target out of the zone: no bonus.
  CHECK(dealt(true, r - 80.0f) == doctest::Approx(dealt(false, r - 80.0f)));
}

TEST_CASE("M2.11 re-aim: costs 10% of the price, between waves only, emits AimChanged") {
  ContentDB db = focusContent();
  // A one-wave set so the run has phases.
  WaveSetDef set;
  set.id = "one";
  set.planet = 0;
  set.sun = 0;
  WaveDef wave;
  wave.wave = 1;
  wave.spawnSeconds = 1.0f;
  wave.spawns.push_back({0, kRed, 1});
  set.waves.push_back(wave);
  db.waveSets.push_back(set);
  SimConfig cfg = sandboxConfig(db);
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();
  w.stardust = 200.0f;
  sim.push(cmd::placeBuilding(0, 3, kGatling, 0.0f));
  sim.step();
  REQUIRE(w.slots[3].type == kGatling);
  const float bank = w.stardust;
  w.events.clear();
  sim.push(cmd::setAim(sim.tick(), 3, 29.0f * kDeg));  // snaps to 30
  sim.step();
  CHECK(w.slots[3].aimOffset == doctest::Approx(30.0f * kDeg));
  CHECK(w.stardust == doctest::Approx(bank - 5.0f));
  REQUIRE(eventsOf(sim, EventType::AimChanged).size() == 1);
  CHECK(eventsOf(sim, EventType::AimChanged)[0].value == doctest::Approx(5.0f));
  // The same offset again: free no-op.
  w.events.clear();
  sim.push(cmd::setAim(sim.tick(), 3, 30.0f * kDeg));
  sim.step();
  CHECK(w.stardust == doctest::Approx(bank - 5.0f));
  CHECK(eventsOf(sim, EventType::AimChanged).empty());
  // Broke: refused as Unaffordable with the price.
  w.stardust = 1.0f;
  sim.push(cmd::setAim(sim.tick(), 3, -30.0f * kDeg));
  sim.step();
  REQUIRE(!eventsOf(sim, EventType::CommandRejected).empty());
  CHECK(eventsOf(sim, EventType::CommandRejected).back().b == static_cast<int32_t>(RejectReason::Unaffordable));
  CHECK(w.slots[3].aimOffset == doctest::Approx(30.0f * kDeg));
  // Mid-wave: refused (WrongPhase).
  w.stardust = 100.0f;
  sim.push(cmd::startWave(sim.tick()));
  sim.step();
  REQUIRE(w.phase == Phase::Wave);
  w.events.clear();
  sim.push(cmd::setAim(sim.tick(), 3, -30.0f * kDeg));
  sim.step();
  REQUIRE(!eventsOf(sim, EventType::CommandRejected).empty());
  CHECK(eventsOf(sim, EventType::CommandRejected).back().b == static_cast<int32_t>(RejectReason::WrongPhase));
  CHECK(w.slots[3].aimOffset == doctest::Approx(30.0f * kDeg));
}

TEST_CASE("M2.11 Sun Lens beams crossing in a zone merge: summed power, pierce") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  place(sim, 1, kLens, 30.0f);
  place(sim, 5, kLens, -30.0f);
  World& w = sim.world();
  REQUIRE(w.focus.size() == 1);
  CHECK(w.focus[0].lenses == 2);
  float fx = 0.0f, fy = 0.0f;
  focusWorldPos(w, 0, fx, fy);
  const float len = std::sqrt(fx * fx + fy * fy);
  // Two enemies beyond the point on the merged beam (the bisector).
  const float ux = fx / len, uy = fy / len;
  const uint32_t a = putEnemy(w, kGreen, fx + ux * 5.0f, fy + uy * 5.0f);
  const uint32_t b = putEnemy(w, kGreen, fx + ux * 25.0f, fy + uy * 25.0f);
  const float ha = w.enemies.hp[a], hb = w.enemies.hp[b];
  sim.step();
  int merged = 0, feeders = 0;
  for (const BeamShot& s : w.beams) {
    if (s.merged) {
      ++merged;
      CHECK(s.power == doctest::Approx(2.0f));  // two full-power Lenses
    } else {
      ++feeders;
      CHECK(s.x1 == doctest::Approx(fx));  // feeders draw up to the focus point
    }
  }
  CHECK(merged == 1);
  CHECK(feeders == 2);
  // Both enemies took the summed damage (pierce), about 2 x 4 dps x 1 tick each.
  const float dt = kFixedDt;
  CHECK(ha - w.enemies.hp[a] == doctest::Approx(8.0f * dt).epsilon(0.3));
  CHECK(hb - w.enemies.hp[b] == doctest::Approx(8.0f * dt).epsilon(0.3));
  // With merging off the Lenses fire their own beams, one target each.
  ContentDB off = db;
  off.rules.focus.lensMerge = false;
  Sim s2(sandboxConfig(off));
  place(s2, 1, kLens, 30.0f);
  place(s2, 5, kLens, -30.0f);
  putEnemy(s2.world(), kGreen, fx + ux * 5.0f, fy + uy * 5.0f);
  s2.step();
  for (const BeamShot& s : s2.world().beams) CHECK_FALSE(s.merged);
}

TEST_CASE("M2.11 focus is deterministic and the solver does not allocate past its reserve") {
  const ContentDB db = focusContent();
  Sim sim(sandboxConfig(db));
  for (int s = 0; s < 36; s += 2) place(sim, s, kGatling, (s % 4 == 0) ? 30.0f : -30.0f);
  const size_t cap = sim.world().focus.capacity();
  solveFocus(sim.world());
  CHECK(sim.world().focus.capacity() == cap);
  CHECK(sim.world().focus.size() <= kMaxFocusPoints);
  Sim again(sandboxConfig(db));
  for (int s = 0; s < 36; s += 2) place(again, s, kGatling, (s % 4 == 0) ? 30.0f : -30.0f);
  REQUIRE(again.world().focus.size() == sim.world().focus.size());
  for (size_t i = 0; i < sim.world().focus.size(); ++i) {
    CHECK(again.world().focus[i].x == sim.world().focus[i].x);
    CHECK(again.world().focus[i].feeders == sim.world().focus[i].feeders);
  }
}
