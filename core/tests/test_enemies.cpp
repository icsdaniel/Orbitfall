// M1.5 acceptance: grid, enemy pool, two lanes, movement, landing formula,
// planet HP and GameOver.
#include <algorithm>
#include <cmath>
#include <vector>

#include "alloc_counter.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "spatial/grid.h"
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

// Steps until no enemy is left or `maxTicks` pass; returns the ticks run.
uint32_t runUntilEmpty(Sim& sim, uint32_t maxTicks) {
  uint32_t n = 0;
  while (sim.world().enemies.count > 0 && n < maxTicks) {
    sim.step();
    ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("grid: counting-sort rebuild matches a brute-force box query") {
  SpatialGrid grid;
  grid.configure(100.0f, 16, 500);
  Rng rng;
  rng.seed(7);
  std::vector<float> x(500), y(500);
  for (int i = 0; i < 500; ++i) {
    x[i] = rng.range(-130.0f, 130.0f);  // some outside the covered square
    y[i] = rng.range(-130.0f, 130.0f);
  }
  grid.rebuild(x.data(), y.data(), 500);
  CHECK(grid.cellStart.back() == 500);

  for (int q = 0; q < 50; ++q) {
    const float x0 = rng.range(-140.0f, 100.0f), y0 = rng.range(-140.0f, 100.0f);
    const float x1 = x0 + rng.range(0.0f, 80.0f), y1 = y0 + rng.range(0.0f, 80.0f);
    std::vector<uint32_t> got;
    grid.query(x0, y0, x1, y1, [&](uint32_t i) {
      if (x[i] >= x0 && x[i] <= x1 && y[i] >= y0 && y[i] <= y1) got.push_back(i);
    });
    std::vector<uint32_t> want;
    for (uint32_t i = 0; i < 500; ++i)
      if (x[i] >= x0 && x[i] <= x1 && y[i] >= y0 && y[i] <= y1) want.push_back(i);
    std::sort(got.begin(), got.end());
    CHECK(got == want);  // every point once, none missed
  }
}

TEST_CASE("grid: rebuild and query do not allocate") {
  SpatialGrid grid;
  grid.configure(100.0f, 16, 64);
  float x[64], y[64];
  for (int i = 0; i < 64; ++i) {
    x[i] = static_cast<float>(i) - 32.0f;
    y[i] = static_cast<float>(i % 7) * 10.0f;
  }
  const uint64_t before = allocationCount();
  uint32_t seen = 0;
  for (int k = 0; k < 10; ++k) {
    grid.rebuild(x, y, 64);
    grid.query(-200.0f, -200.0f, 200.0f, 200.0f, [&](uint32_t) { ++seen; });
  }
  CHECK(allocationCount() == before);
  CHECK(seen == 640);
}

TEST_CASE("enemies: spawn on the two lane bearings at the spawn radius, heading inward") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t a = spawnEnemy(w, kRed, 0);
  const uint32_t b = spawnEnemy(w, kDart, 1);
  REQUIRE(a == 0);
  REQUIRE(b == 1);
  CHECK(w.enemies.posX[a] == doctest::Approx(900.0f * std::cos(30.0f * kDeg)));
  CHECK(w.enemies.posY[a] == doctest::Approx(900.0f * std::sin(30.0f * kDeg)));
  CHECK(std::atan2(w.enemies.posY[b], w.enemies.posX[b]) == doctest::Approx(170.0f * kDeg));
  CHECK(std::hypot(w.enemies.velX[b], w.enemies.velY[b]) == doctest::Approx(110.0f));
  CHECK(w.enemies.lane[b] == 1);
  CHECK(w.enemies.type[b] == kDart);
  CHECK(w.enemiesSpawned == 2);
  CHECK(spawnEnemy(w, 99, 0) == kInvalidIndex);
  CHECK(spawnEnemy(w, kRed, 2) == kInvalidIndex);

  // One second of movement brings each one `speed` closer.
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(std::hypot(w.enemies.posX[0], w.enemies.posY[0]) == doctest::Approx(860.0f).epsilon(1e-4));
  CHECK(std::hypot(w.enemies.posX[1], w.enemies.posY[1]) == doctest::Approx(790.0f).epsilon(1e-4));
  CHECK(w.grid.count == 2);
}

TEST_CASE("landing: damage is landDamagePerLayer x remaining layers, flat for non-layered") {
  ContentDB db = tinyContent();
  db.enemies[kGreen].landDamagePerLayer = 2.0f;
  struct Case { int type; float damage; };
  const Case cases[] = {{kRed, 1.0f}, {kBlue, 2.0f}, {kGreen, 6.0f}, {kDart, 1.0f}, {kEclipser, 15.0f}};
  for (const Case& c : cases) {
    Sim sim(sandboxConfig(db));
    World& w = sim.world();
    const uint32_t i = spawnEnemy(w, c.type, 0);
    REQUIRE(i != kInvalidIndex);
    // Put it just outside the surface so the test is short.
    const float bearing = db.planets[0].lanes[0].bearing;
    w.enemies.posX[i] = 105.0f * std::cos(bearing);
    w.enemies.posY[i] = 105.0f * std::sin(bearing);
    CHECK(runUntilEmpty(sim, 60) < 60);
    const auto hits = eventsOf(sim, EventType::PlanetHit);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].a == c.type);
    CHECK(hits[0].b == 0);
    CHECK(hits[0].value == doctest::Approx(c.damage));
    CHECK(w.planetHp == doctest::Approx(100.0f - c.damage));
    CHECK(std::hypot(hits[0].x, hits[0].y) <= 100.0f);
  }
}

TEST_CASE("landing: an enemy travels the whole lane and lands when it reaches R") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  spawnEnemy(sim.world(), kDart, 1);
  const uint32_t ticks = runUntilEmpty(sim, 400);
  // (900 - 100) / 110 = 7.27 s = 218.2 ticks.
  CHECK(ticks == 219);
  CHECK(sim.world().planetHp == 99.0f);
}

TEST_CASE("landing: planet HP 0 emits GameOver once and freezes the run") {
  ContentDB db = tinyContent();
  db.planets[0].hp = 16.0f;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  for (int k = 0; k < 3; ++k) {
    const uint32_t i = spawnEnemy(w, kEclipser, 0);
    w.enemies.posX[i] = 100.5f;  // all land on the same tick
    w.enemies.posY[i] = 0.0f;
    w.enemies.velX[i] = -25.0f;
    w.enemies.velY[i] = 0.0f;
  }
  const uint32_t far = spawnEnemy(w, kRed, 1);
  for (int i = 0; i < 10 && w.phase == Phase::Wave; ++i) sim.step();
  CHECK(w.phase == Phase::Lost);
  CHECK(w.planetHp == 0.0f);
  CHECK(eventsOf(sim, EventType::GameOver).size() == 1);
  CHECK(far == 3);

  // Frozen: the survivor stops moving and the planet stops turning.
  REQUIRE(w.enemies.count == 1);
  const float x = w.enemies.posX[0];
  sim.push(cmd::rotateInput(sim.tick(), 1.0f));
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.enemies.posX[0] == x);
  CHECK(w.planetRotation == 0.0f);
  CHECK(eventsOf(sim, EventType::GameOver).size() == 1);
}

TEST_CASE("enemies: a tick with many enemies does not allocate; a full pool refuses spawns") {
  const ContentDB db = tinyContent();
  SimConfig cfg = sandboxConfig(db);
  cfg.enemyCapacity = 300;
  Sim sim(cfg);
  World& w = sim.world();
  for (int i = 0; i < 300; ++i) REQUIRE(spawnEnemy(w, i % 4, i % 2) != kInvalidIndex);
  CHECK(spawnEnemy(w, kRed, 0) == kInvalidIndex);
  CHECK(w.enemies.overflowCount == 1);
  sim.step();
  const uint64_t before = allocationCount();
  for (int i = 0; i < 20; ++i) sim.step();
  CHECK(allocationCount() == before);
}
