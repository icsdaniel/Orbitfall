// Render snapshot: enemies and projectiles in the 16-float BatchRenderer layout.
#include <cmath>
#include <vector>

#include "alloc_counter.h"
#include "doctest/doctest.h"
#include "sim/render_snapshot.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

TEST_CASE("render snapshot: enemies carry transform, shape, colour tier and traits from content") {
  ContentDB db = tinyContent();
  db.enemies[kRed].visual.colorTier = 0;
  db.enemies[kGreen].visual.colorTier = 2;
  db.enemies[kDart].visual.shape = Shape::Triangle;
  db.enemies[kDart].visual.colorTier = 0;
  db.enemies[kEclipser].visual.shape = Shape::Disc;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t green = spawnEnemy(w, kGreen, 0);
  const uint32_t dart = spawnEnemy(w, kDart, 1);
  const uint32_t big = spawnEnemy(w, kEclipser, 0);
  w.enemies.hp[big] = 10.0f;
  w.enemies.posX[dart] = 500.0f;
  w.enemies.posY[dart] = 0.0f;
  w.enemies.velX[dart] = -110.0f;
  w.enemies.velY[dart] = 0.0f;

  std::vector<float> buf(3 * kRenderFloatsPerInstance, -99.0f);
  const float colors[5 * 4] = {1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0.1f, 0.1f, 0.2f, 1};
  RenderParams params;
  params.enemyColors = colors;
  const uint64_t before = allocationCount();
  REQUIRE(fillEnemyRenderBuffer(w, buf.data(), 3, params) == 3);
  CHECK(allocationCount() == before);

  const float* g = &buf[green * 16];
  CHECK(g[3] == w.enemies.posX[green]);  // origin.x
  CHECK(g[7] == w.enemies.posY[green]);  // origin.y
  CHECK(std::hypot(g[0], g[4]) == doctest::Approx(8.0f));  // basis scaled by the radius
  CHECK(std::hypot(g[1], g[5]) == doctest::Approx(8.0f));
  CHECK(g[2] == 0.0f);
  CHECK(g[6] == 0.0f);
  CHECK(g[9] == 1.0f);  // its colour entry (green)
  CHECK(g[12] == static_cast<float>(static_cast<int>(Shape::Circle)));
  CHECK(g[13] == 2.0f);  // colour tier
  CHECK(g[14] == 0.0f);
  CHECK(g[15] == 1.0f);  // full layer

  const float* d = &buf[dart * 16];
  CHECK(d[12] == static_cast<float>(static_cast<int>(Shape::Triangle)));
  CHECK(d[13] == 0.0f);
  CHECK(d[14] == static_cast<float>(kTraitFast));
  // Moving along -x: the shape's up (0, -1) maps onto (-1, 0).
  const float upX = d[1] * -1.0f, upY = d[5] * -1.0f;  // basis * (0, -1)
  CHECK(upX == doctest::Approx(-6.0f));
  CHECK(upY == doctest::Approx(0.0f));
  // A proper rotation: the axes are perpendicular and not mirrored.
  CHECK(d[0] * d[1] + d[4] * d[5] == doctest::Approx(0.0f));
  CHECK(d[0] * d[5] - d[1] * d[4] > 0.0f);

  const float* b = &buf[big * 16];
  CHECK(b[12] == static_cast<float>(static_cast<int>(Shape::Disc)));
  CHECK(b[13] == -1.0f);  // not a tier colour: instance colour only
  CHECK(b[8] == doctest::Approx(0.1f));
  CHECK(b[15] == doctest::Approx(0.25f));  // 10 of 40 hp

  // Capacity is respected; no colour table means white.
  std::vector<float> one(16, 0.0f);
  CHECK(fillEnemyRenderBuffer(w, one.data(), 1) == 1);
  CHECK(one[8] == 1.0f);
  CHECK(fillEnemyRenderBuffer(w, nullptr, 3) == 0);
}

TEST_CASE("render snapshot: alpha steps positions back toward the previous tick") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = spawnEnemy(w, kDart, 0);
  w.enemies.posX[i] = 500.0f;
  w.enemies.posY[i] = 0.0f;
  w.enemies.velX[i] = -110.0f;
  w.enemies.velY[i] = 0.0f;
  const float before = w.enemies.posX[i];
  sim.step();
  const float after = w.enemies.posX[i];
  float buf[16];
  RenderParams params;
  params.alpha = 1.0f;
  fillEnemyRenderBuffer(w, buf, 1, params);
  CHECK(buf[3] == after);
  params.alpha = 0.0f;
  fillEnemyRenderBuffer(w, buf, 1, params);
  CHECK(buf[3] == doctest::Approx(before));
  params.alpha = 0.5f;
  fillEnemyRenderBuffer(w, buf, 1, params);
  CHECK(buf[3] == doctest::Approx(0.5f * (before + after)));
  // Frozen (run over): no interpolation.
  w.phase = Phase::Lost;
  params.alpha = 0.0f;
  fillEnemyRenderBuffer(w, buf, 1, params);
  CHECK(buf[3] == after);
}

TEST_CASE("render snapshot: projectiles") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  w.stardust = 1000.0f;
  sim.push(cmd::placeBuilding(0, 0, kCannon, 0.0f));
  sim.step();
  const uint32_t e = spawnEnemy(w, kEclipser, 0);
  w.enemies.posX[e] = 400.0f;
  w.enemies.posY[e] = 0.0f;
  sim.step();
  REQUIRE(w.projectiles.count == 1);
  float buf[32];
  REQUIRE(fillProjectileRenderBuffer(w, buf, 2) == 1);
  CHECK(buf[3] == w.projectiles.posX[0]);
  CHECK(buf[7] == w.projectiles.posY[0]);
  CHECK(std::hypot(buf[0], buf[4]) == doctest::Approx(4.0f));  // shell radius
  CHECK(buf[8] == 1.0f);
  CHECK(buf[12] == 0.0f);   // circle
  CHECK(buf[13] == -1.0f);  // instance colour
  CHECK(buf[15] == static_cast<float>(static_cast<int>(ProjectileKind::Shell)));
}
