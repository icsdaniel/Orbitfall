#include <vector>

#include "doctest/doctest.h"
#include "sim/sim.h"

using namespace orbitfall;

namespace {

// Test system: consumes the world RNG and emits events so both feed the hash.
void rngNoise(World& world, float dt) {
  const uint32_t v = world.rng.nextU32();
  if (dt > 0.0f && (v & 15u) == 0u) {
    world.emit(EventType::StardustChanged, static_cast<int32_t>(v >> 16), 0, 0.0f, 0.0f,
               world.rng.nextFloat());
  }
}

// A varied command log over `ticks` ticks, built from its own seeded generator.
std::vector<Command> makeLog(uint32_t logSeed, uint32_t ticks) {
  std::vector<Command> log;
  Rng r;
  r.seed(logSeed);
  for (uint32_t t = 0; t < ticks; ++t) {
    if (r.nextBelow(4) == 0) log.push_back(cmd::rotateInput(t, r.range(-1.0f, 1.0f)));
    if (r.nextBelow(40) == 0) {
      log.push_back(cmd::placeBuilding(t, static_cast<int32_t>(r.nextBelow(36)),
                                       static_cast<int32_t>(r.nextBelow(3)), r.range(-0.5f, 0.5f)));
    }
    if (r.nextBelow(90) == 0) log.push_back(cmd::sellBuilding(t, static_cast<int32_t>(r.nextBelow(36))));
    if (r.nextBelow(70) == 0) {
      log.push_back(cmd::setAim(t, static_cast<int32_t>(r.nextBelow(36)), r.range(-0.5f, 0.5f)));
    }
    if (r.nextBelow(120) == 0) {
      log.push_back(cmd::setTargeting(t, static_cast<int32_t>(r.nextBelow(36)),
                                      static_cast<uint8_t>(r.nextBelow(3))));
    }
    if (r.nextBelow(400) == 0) log.push_back(cmd::startWave(t));
    if (r.nextBelow(150) == 0) {
      const float scales[4] = {0.0f, 0.75f, 1.0f, 2.0f};
      log.push_back(cmd::setTimeScale(t, scales[r.nextBelow(4)], r.nextBelow(2) ? 0.3f : 0.0f));
    }
  }
  return log;
}

uint64_t runUpfront(uint32_t seed, const std::vector<Command>& log, uint32_t ticks) {
  SimConfig cfg;
  cfg.seed = seed;
  Sim sim(cfg);
  sim.pipeline().add("rngNoise", rngNoise);
  for (const Command& c : log) sim.push(c);
  for (uint32_t i = 0; i < ticks; ++i) sim.step();
  return sim.hash();
}

// Feeds commands just before their tick and drains events as a host would.
uint64_t runStreamed(uint32_t seed, const std::vector<Command>& log, uint32_t ticks) {
  SimConfig cfg;
  cfg.seed = seed;
  Sim sim(cfg);
  sim.pipeline().add("rngNoise", rngNoise);
  size_t next = 0;
  for (uint32_t i = 0; i < ticks; ++i) {
    while (next < log.size() && log[next].tick <= sim.tick()) sim.push(log[next++]);
    sim.step();
    if (i % 7 == 0) sim.world().events.clear();
  }
  return sim.hash();
}

}  // namespace

TEST_CASE("determinism: 3,000 ticks twice from the same seed and command log give equal hashes") {
  const uint32_t ticks = 3000;
  const std::vector<Command> log = makeLog(99, ticks);
  REQUIRE(log.size() > 500);

  const uint64_t a = runUpfront(42, log, ticks);
  const uint64_t b = runUpfront(42, log, ticks);
  CHECK(a == b);

  SUBCASE("a different seed gives a different hash") { CHECK(runUpfront(43, log, ticks) != a); }
  SUBCASE("a different command log gives a different hash") {
    CHECK(runUpfront(42, makeLog(100, ticks), ticks) != a);
    std::vector<Command> shorter = log;
    shorter.pop_back();
    CHECK(runUpfront(42, shorter, ticks) != a);
  }
  SUBCASE("feeding the log tick by tick and draining events does not change the hash") {
    CHECK(runStreamed(42, log, ticks) == a);
  }
  SUBCASE("the hash changes as the sim runs") { CHECK(runUpfront(42, log, ticks - 1) != a); }
}

TEST_CASE("fixed step: advance runs whole 30 Hz ticks and keeps the remainder") {
  Sim sim;
  CHECK(sim.advance(0.0) == 0);
  CHECK(sim.advance(-1.0) == 0);
  CHECK(sim.advance(0.02) == 0);  // less than one tick
  CHECK(sim.tick() == 0);
  CHECK(sim.advance(0.02) == 1);  // 0.04 s accumulated
  CHECK(sim.tick() == 1);
  CHECK(sim.alpha() >= 0.0f);
  CHECK(sim.alpha() < 1.0f);
  CHECK(sim.alpha() == doctest::Approx((0.04 - 1.0 / 30.0) * 30.0).epsilon(0.001));

  uint32_t total = 1;
  for (int i = 0; i < 600; ++i) total += sim.advance(1.0 / 60.0);  // 10 s at 60 fps
  CHECK(total >= 300);
  CHECK(total <= 301);
  CHECK(sim.world().simTime == doctest::Approx(total / 30.0).epsilon(1e-6));
}

TEST_CASE("fixed step: a long hitch is capped and the backlog dropped") {
  Sim sim;
  sim.maxTicksPerAdvance = 5;
  CHECK(sim.advance(10.0) == 5);
  CHECK(sim.alpha() == 0.0f);
  CHECK(sim.advance(1.0 / 30.0 + 1e-6) == 1);
}

TEST_CASE("fixed step: step count does not depend on how real time is sliced") {
  SimConfig cfg;
  cfg.seed = 7;
  Sim a(cfg), b(cfg);
  for (int i = 0; i < 90; ++i) a.step();
  while (b.tick() < 90) b.advance(0.0041);
  CHECK(a.hash() == b.hash());
}

TEST_CASE("time scale: pause freezes sim time while ticks keep counting") {
  Sim sim;
  sim.push(cmd::setTimeScale(10, 0.0f));
  for (int i = 0; i < 10; ++i) sim.step();
  const double before = sim.world().simTime;
  CHECK(before == doctest::Approx(10.0 / 30.0));
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(sim.tick() == 40);
  CHECK(sim.world().dt == 0.0f);
  CHECK(sim.world().simTime == before);

  // Commands still apply while paused, including the one that resumes.
  sim.push(cmd::rotateInput(sim.tick(), 0.5f));
  sim.push(cmd::setTimeScale(sim.tick(), 2.0f));
  sim.step();
  CHECK(sim.world().rotateInput == 0.5f);
  CHECK(sim.world().dt == doctest::Approx(2.0f / 30.0f));
  CHECK(sim.world().simTime == doctest::Approx(before + 2.0 / 30.0));
}

TEST_CASE("time scale: eased change is monotonic and lands exactly on the target") {
  Sim sim;
  sim.push(cmd::setTimeScale(0, 0.75f, 0.3f));  // 0.3 s = 9 ticks
  float prev = 1.0f;
  for (int i = 0; i < 9; ++i) {
    sim.step();
    const float s = sim.world().timeScale;
    CHECK(s < prev);
    CHECK(s >= 0.75f);
    prev = s;
    if (i < 8) CHECK(s > 0.75f);
  }
  CHECK(sim.world().timeScale == 0.75f);
  sim.step();
  CHECK(sim.world().timeScale == 0.75f);
  CHECK(sim.world().dt == doctest::Approx(0.75f / 30.0f));
}

TEST_CASE("time scale: invalid values are clamped or ignored") {
  Sim sim;
  sim.push(cmd::setTimeScale(0, -3.0f));
  sim.step();
  CHECK(sim.world().timeScale == 0.0f);
  sim.push(cmd::setTimeScale(1, 1000.0f));
  sim.step();
  CHECK(sim.world().timeScale == kMaxTimeScale);
  const float zero = 0.0f;
  sim.push(cmd::setTimeScale(2, zero / zero));  // NaN
  sim.step();
  CHECK(sim.world().timeScale == kMaxTimeScale);
  sim.push(cmd::rotateInput(3, 9.0f));
  sim.step();
  CHECK(sim.world().rotateInput == 1.0f);
}

TEST_CASE("commands: apply on their tick, in tick order, stable for equal ticks") {
  Sim sim;
  // Pushed out of order on purpose.
  sim.push(cmd::rotateInput(5, 0.5f));
  sim.push(cmd::rotateInput(2, -1.0f));
  sim.push(cmd::rotateInput(5, 0.25f));  // same tick as the first: applies after it
  CHECK(sim.world().commands.pending() == 3);

  sim.step();  // tick 0
  sim.step();  // tick 1
  CHECK(sim.world().rotateInput == 0.0f);
  sim.step();  // tick 2
  CHECK(sim.world().rotateInput == -1.0f);
  CHECK(sim.world().commandsApplied == 1);
  sim.step();
  sim.step();
  CHECK(sim.world().rotateInput == -1.0f);
  sim.step();  // tick 5
  CHECK(sim.world().rotateInput == 0.25f);
  CHECK(sim.world().commandsApplied == 3);
  CHECK(sim.world().commands.pending() == 0);
}

TEST_CASE("commands: a late command applies on the next tick") {
  Sim sim;
  for (int i = 0; i < 10; ++i) sim.step();
  sim.push(cmd::rotateInput(3, 1.0f));
  sim.step();
  CHECK(sim.world().rotateInput == 1.0f);
}

TEST_CASE("commands: every command kind changes the hash") {
  const Command kinds[] = {
      cmd::rotateInput(1, 0.5f),  cmd::placeBuilding(1, 4, 1, 0.1f), cmd::sellBuilding(1, 4),
      cmd::setAim(1, 4, 0.2f),    cmd::setTargeting(1, 4, 2),        cmd::startWave(1),
      cmd::setTimeScale(1, 0.5f),
  };
  Sim base;
  for (int i = 0; i < 5; ++i) base.step();
  std::vector<uint64_t> seen;
  seen.push_back(base.hash());
  for (const Command& c : kinds) {
    Sim sim;
    sim.push(c);
    for (int i = 0; i < 5; ++i) sim.step();
    const uint64_t h = sim.hash();
    for (uint64_t other : seen) CHECK(h != other);
    seen.push_back(h);
  }
}

TEST_CASE("commands: the queue survives many push/pop cycles") {
  CommandQueue q;
  Command out;
  for (uint32_t t = 0; t < 5000; ++t) {
    q.push(cmd::rotateInput(t + 2, 0.0f));
    q.push(cmd::startWave(t + 1));
    uint32_t last = 0;
    while (q.popDue(t, out)) {
      CHECK(out.tick >= last);
      CHECK(out.tick <= t);
      last = out.tick;
    }
  }
  CHECK(q.pending() == 3);
  CHECK(q.items.size() < 1024);
}

TEST_CASE("events: emitted with the current tick and drained by the host") {
  Sim sim;
  for (int i = 0; i < 3; ++i) sim.step();
  World& w = sim.world();
  const uint64_t before = sim.hash();
  w.emit(EventType::PlanetHit, 7, 0, 10.0f, -5.0f, 3.0f);
  w.emit(EventType::GameOver);
  REQUIRE(w.events.size() == 2);
  CHECK(w.events.items[0].type == EventType::PlanetHit);
  CHECK(w.events.items[0].tick == 3);
  CHECK(w.events.items[0].a == 7);
  CHECK(w.events.items[0].value == 3.0f);
  CHECK(w.events.items[1].type == EventType::GameOver);
  CHECK(sim.hash() != before);

  const uint64_t withEvents = sim.hash();
  w.events.clear();
  CHECK(w.events.size() == 0);
  CHECK(w.events.totalEmitted == 2);
  CHECK(sim.hash() == withEvents);  // draining is not a state change
}

TEST_CASE("pipeline: default order starts with applyCommands; reset restarts the run") {
  Pipeline p = defaultPipeline();
  REQUIRE(p.count >= 2);
  CHECK(p.systems[0].fn == applyCommands);
  CHECK(p.add("null", nullptr) == false);
  while (p.count < Pipeline::kMaxSystems) CHECK(p.add("advanceClock", advanceClock));
  CHECK(p.add("advanceClock", advanceClock) == false);

  SimConfig cfg;
  cfg.seed = 5;
  Sim sim(cfg);
  const uint64_t start = sim.hash();
  sim.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 20; ++i) sim.advance(0.05);
  CHECK(sim.hash() != start);
  sim.reset(cfg);
  CHECK(sim.tick() == 0);
  CHECK(sim.hash() == start);
}
