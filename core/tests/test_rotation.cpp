// M1.1 acceptance: accel, max speed and damping tests, numbers from PlanetDef.
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

TEST_CASE("rotation: input accelerates at rotAccel") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::rotateInput(0, 1.0f));
  sim.step();
  CHECK(sim.world().planetAngVel == doctest::Approx(db.planets[0].rotAccel * kFixedDt));
  for (int i = 0; i < 2; ++i) sim.step();
  CHECK(sim.world().planetAngVel == doctest::Approx(db.planets[0].rotAccel * 3.0f * kFixedDt));
  CHECK(sim.world().planetRotation > 0.0f);
}

TEST_CASE("rotation: speed is capped at input * rotMaxSpeed, both ways") {
  const ContentDB db = tinyContent();
  const float maxSpeed = db.planets[0].rotMaxSpeed;
  Sim sim(sandboxConfig(db));
  sim.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 60; ++i) {
    sim.step();
    CHECK(sim.world().planetAngVel <= maxSpeed + 1e-6f);
  }
  CHECK(sim.world().planetAngVel == doctest::Approx(maxSpeed));
  // 90 deg/s reached after 0.25 s at 360 deg/s^2: 8 ticks.
  sim.push(cmd::rotateInput(sim.tick(), -0.5f));
  for (int i = 0; i < 60; ++i) sim.step();
  CHECK(sim.world().planetAngVel == doctest::Approx(-0.5f * maxSpeed));
}

TEST_CASE("rotation: released input decays with rotDamping") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 30; ++i) sim.step();
  sim.push(cmd::rotateInput(sim.tick(), 0.0f));
  float prev = sim.world().planetAngVel;
  sim.step();
  CHECK(sim.world().planetAngVel == doctest::Approx(prev * (1.0f - db.planets[0].rotDamping * kFixedDt)));
  for (int i = 0; i < 29; ++i) {
    prev = sim.world().planetAngVel;
    sim.step();
    CHECK(sim.world().planetAngVel < prev);
    CHECK(sim.world().planetAngVel >= 0.0f);
  }
  // One second at damping 4/s leaves about e^-4 of the speed.
  CHECK(sim.world().planetAngVel < 0.03f * db.planets[0].rotMaxSpeed);
}

TEST_CASE("rotation: angle stays in [0, 2pi) and integrates the speed") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::rotateInput(0, -1.0f));
  sim.step();
  CHECK(sim.world().planetRotation > kPi);  // wrapped below zero
  for (int i = 0; i < 600; ++i) {
    sim.step();
    CHECK(sim.world().planetRotation >= 0.0f);
    CHECK(sim.world().planetRotation < kTau);
  }
}

TEST_CASE("rotation: paused time does not turn the planet; no content is a no-op") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::setTimeScale(0, 0.0f));
  sim.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 10; ++i) sim.step();
  CHECK(sim.world().planetRotation == 0.0f);
  CHECK(sim.world().planetAngVel == 0.0f);

  Sim bare;
  bare.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 10; ++i) bare.step();
  CHECK(bare.world().planetRotation == 0.0f);
}
