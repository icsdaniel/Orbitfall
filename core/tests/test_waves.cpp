// M1.9 acceptance (sim part): waves from content; intermission freeze; Start
// Wave; spawns spread over spawnSeconds; WaveCleared and Victory; the forecast
// matches the actual spawns for all 8 shipped waves; a dim wave weakens beam
// and income; replay determinism over a full scripted run.
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "systems/waves.h"
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

// tinyContent plus a two-wave set.
ContentDB waveContent() {
  ContentDB db = tinyContent();
  WaveSetDef ws;
  ws.id = "test";
  ws.planet = 0;
  ws.sun = 0;
  WaveDef w1;
  w1.wave = 1;
  w1.lightMult = 1.0f;
  w1.spawnSeconds = 4.0f;
  w1.spawns = {{0, kDart, 4}, {1, kDart, 2}};
  WaveDef w2;
  w2.wave = 2;
  w2.lightMult = 0.5f;
  w2.spawnSeconds = 2.0f;
  w2.spawns = {{1, kDart, 3}};
  ws.waves = {w1, w2};
  db.waveSets.push_back(ws);
  db.waveSetIds[ws.id] = 0;
  return db;
}

SimConfig waveConfig(const ContentDB& db, uint32_t seed = 1) {
  SimConfig cfg = sandboxConfig(db, seed);
  cfg.waveSet = 0;
  return cfg;
}

const ContentDB& shipped() {
  static ContentDB db;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
    loaded = true;
  }
  return db;
}

// Steps until the phase leaves Wave; returns the ticks run.
uint32_t runWave(Sim& sim, uint32_t maxTicks = 20000) {
  uint32_t n = 0;
  while (sim.world().phase == Phase::Wave && n < maxTicks) {
    sim.step();
    ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("waves: a run starts in an intermission with the sim frozen") {
  const ContentDB db = waveContent();
  Sim sim(waveConfig(db));
  World& w = sim.world();
  CHECK(w.phase == Phase::Intermission);
  CHECK(w.waveIndex == 0);
  CHECK(w.waveCount() == 2);
  w.stardust = 1000.0f;
  sim.push(cmd::placeBuilding(0, 0, kPanel, 0.0f));
  sim.push(cmd::placeBuilding(0, 1, kGatling, 0.0f));
  sim.push(cmd::sellBuilding(5, 1));
  sim.push(cmd::rotateInput(10, 1.0f));
  for (int i = 0; i < 90; ++i) sim.step();
  // Build, sell and rotate work ...
  CHECK(w.slots[0].type == kPanel);
  CHECK(w.slots[1].type == -1);
  CHECK(w.planetRotation > 1.0f);
  // ... but the sun, the spawner and the income stand still.
  CHECK(w.sunBearing == 0.0f);
  CHECK(w.enemies.count == 0);
  CHECK(w.waveTime == 0.0f);
  CHECK(w.stardust == doctest::Approx(1000.0f - 40.0f - 50.0f + 35.0f));
  // Lit flags still follow the turning planet so the player can see the beam.
  uint32_t lit = 0;
  for (const BuildingSlot& b : w.slots) lit += (b.flags & kSlotLit) ? 1 : 0;
  CHECK(lit >= 8);
  CHECK(lit <= 10);
}

TEST_CASE("waves: rotation between waves ignores the time scale; pause in a wave stops it") {
  const ContentDB db = waveContent();
  Sim sim(waveConfig(db));
  sim.push(cmd::setTimeScale(0, 0.0f));
  sim.push(cmd::rotateInput(0, 1.0f));
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(sim.world().planetRotation > 0.5f);
}

TEST_CASE("waves: StartWave spreads each entry's spawns over spawnSeconds") {
  const ContentDB db = waveContent();
  Sim sim(waveConfig(db));
  World& w = sim.world();
  sim.push(cmd::startWave(0));
  sim.step();
  CHECK(w.phase == Phase::Wave);
  const auto started = eventsOf(sim, EventType::WaveStarted);
  REQUIRE(started.size() == 1);
  CHECK(started[0].a == 0);
  CHECK(started[0].b == 1);
  CHECK(started[0].value == 1.0f);
  // First unit of each entry at once; lane A every 1 s, lane B every 2 s.
  CHECK(w.enemies.count == 2);
  std::vector<uint32_t> countAt;
  for (int i = 1; i < 150; ++i) {
    sim.step();
    if (i % 30 == 15) countAt.push_back(w.enemiesSpawned);
  }
  CHECK(countAt == std::vector<uint32_t>({2, 3, 5, 6, 6}));
  CHECK(w.spawned[0] == 4);
  CHECK(w.spawned[1] == 2);
  // On their lanes.
  uint32_t laneA = 0, laneB = 0;
  for (uint32_t i = 0; i < w.enemies.count; ++i) {
    const float bearing = std::atan2(w.enemies.posY[i], w.enemies.posX[i]);
    if (w.enemies.lane[i] == 0) {
      ++laneA;
      CHECK(bearing == doctest::Approx(30.0f * kDeg));
    } else {
      ++laneB;
      CHECK(bearing == doctest::Approx(170.0f * kDeg));
    }
  }
  CHECK(laneA == 4);
  CHECK(laneB == 2);
  // A second StartWave during the wave is refused.
  sim.push(cmd::startWave(sim.tick()));
  sim.step();
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::WrongPhase));
}

TEST_CASE("waves: cleared wave returns to an intermission, the last one wins") {
  ContentDB db = waveContent();
  Sim sim(waveConfig(db));
  World& w = sim.world();
  sim.push(cmd::startWave(0));
  sim.step();
  CHECK(runWave(sim) < 20000);  // nobody defends: all six darts land
  CHECK(w.phase == Phase::Intermission);
  CHECK(w.waveIndex == 1);
  CHECK(w.planetHp == 94.0f);
  CHECK(w.projectiles.count == 0);
  REQUIRE(eventsOf(sim, EventType::WaveCleared).size() == 1);
  CHECK(eventsOf(sim, EventType::WaveCleared)[0].a == 0);
  CHECK(eventsOf(sim, EventType::Victory).empty());
  CHECK(w.lightMult == 0.5f);  // the next wave's light is already shown

  // Frozen again until the next StartWave.
  const float sun = w.sunBearing;
  for (int i = 0; i < 60; ++i) sim.step();
  CHECK(w.sunBearing == sun);
  CHECK(w.enemies.count == 0);

  sim.push(cmd::startWave(sim.tick()));
  sim.step();
  CHECK(w.phase == Phase::Wave);
  runWave(sim);
  CHECK(w.phase == Phase::Won);
  CHECK(w.waveIndex == 2);
  CHECK(eventsOf(sim, EventType::WaveCleared).size() == 2);
  REQUIRE(eventsOf(sim, EventType::Victory).size() == 1);
  CHECK(w.planetHp == 91.0f);

  // After the run: nothing more is accepted.
  sim.push(cmd::startWave(sim.tick()));
  sim.push(cmd::placeBuilding(sim.tick(), 3, kPanel, 0.0f));
  sim.step();
  CHECK(w.phase == Phase::Won);
  CHECK(w.slots[3].type == -1);
  CHECK(eventsOf(sim, EventType::CommandRejected).size() == 2);
}

TEST_CASE("waves: losing mid-wave ends the run, no WaveCleared, no Victory") {
  ContentDB db = waveContent();
  db.planets[0].hp = 3.0f;
  Sim sim(waveConfig(db));
  sim.push(cmd::startWave(0));
  sim.step();
  runWave(sim);
  CHECK(sim.world().phase == Phase::Lost);
  CHECK(eventsOf(sim, EventType::GameOver).size() == 1);
  CHECK(eventsOf(sim, EventType::GameOver)[0].a == 0);
  CHECK(eventsOf(sim, EventType::WaveCleared).empty());
  for (int i = 0; i < 600; ++i) sim.step();
  CHECK(sim.world().phase == Phase::Lost);
  CHECK(eventsOf(sim, EventType::Victory).empty());
}

TEST_CASE("waves: auto-start timer from content, off at 0") {
  ContentDB db = waveContent();
  db.waveSets[0].autoStartSeconds = 2.0f;
  Sim sim(waveConfig(db));
  for (int i = 0; i < 59; ++i) sim.step();
  CHECK(sim.world().phase == Phase::Intermission);
  sim.step();
  sim.step();
  CHECK(sim.world().phase == Phase::Wave);
}

TEST_CASE("waves: a dim wave weakens the beam and the income") {
  ContentDB db = waveContent();
  db.suns[0].type = SunPathType::Fixed;
  db.waveSets[0].waves[0].spawnSeconds = 10.0f;  // long enough to measure 5 s
  db.waveSets[0].waves[1].spawnSeconds = 10.0f;
  Sim sim(waveConfig(db));
  World& w = sim.world();
  w.stardust = 40.0f;
  sim.push(cmd::placeBuilding(0, 0, kPanel, 0.0f));
  sim.push(cmd::startWave(1));
  for (int i = 0; i < 2; ++i) sim.step();
  CHECK(w.beamStrength == 1.0f);
  float before = w.stardust;
  for (int i = 0; i < 150; ++i) sim.step();
  const float fullLight = w.stardust - before;
  CHECK(fullLight == doctest::Approx(15.0f).epsilon(0.01));
  runWave(sim);
  REQUIRE(w.phase == Phase::Intermission);
  before = w.stardust;
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.stardust == before);  // no income between waves

  sim.push(cmd::startWave(sim.tick()));
  sim.step();
  sim.step();
  CHECK(w.beamStrength == 0.5f);  // wave 2: lightMult 0.5
  CHECK(w.incomePerSecond == doctest::Approx(1.5f));
  before = w.stardust;
  for (int i = 0; i < 150; ++i) sim.step();
  CHECK(w.stardust - before == doctest::Approx(0.5f * fullLight).epsilon(0.01));
}

TEST_CASE("forecast: next wave exact, then icons, then light only") {
  ContentDB db = waveContent();
  // Five waves to see every range: exactAhead 1, iconsAhead 3.
  WaveSetDef& ws = db.waveSets[0];
  for (int k = 3; k <= 5; ++k) {
    WaveDef w = ws.waves[1];
    w.wave = k;
    w.lightMult = 0.1f * static_cast<float>(k);
    ws.waves.push_back(w);
  }
  Sim sim(waveConfig(db));
  const std::vector<WaveForecast> f = forecast(sim.world(), 10);
  REQUIRE(f.size() == 5);
  CHECK(f[0].wave == 1);
  CHECK(f[0].exact);
  REQUIRE(f[0].entries.size() == 2);
  CHECK(f[0].entries[0].lane == 0);
  CHECK(f[0].entries[0].laneBearing == doctest::Approx(30.0f * kDeg));
  CHECK(f[0].entries[0].enemy == kDart);
  CHECK(f[0].entries[0].count == 4);
  CHECK(f[0].entries[1].count == 2);
  CHECK_FALSE(f[0].entries[0].shadowCaster);
  CHECK_FALSE(f[1].exact);
  REQUIRE(f[1].entries.size() == 1);
  CHECK(f[1].entries[0].count == -1);  // icons without counts
  CHECK(f[1].entries[0].enemy == kDart);
  CHECK(f[1].lightMult == 0.5f);
  CHECK(f[2].entries.size() == 1);
  CHECK(f[3].entries.empty());  // beyond iconsAhead: light level only
  CHECK(f[3].lightMult == doctest::Approx(0.4f));
  CHECK(f[4].entries.empty());
  CHECK(forecast(sim.world(), 2).size() == 2);

  // During wave 1 the strip moves on to wave 2.
  sim.push(cmd::startWave(0));
  sim.step();
  const std::vector<WaveForecast> during = forecast(sim.world(), 10);
  REQUIRE(during.size() == 4);
  CHECK(during[0].wave == 2);
  CHECK(during[0].exact);
  CHECK(during[0].entries[0].count == 3);
}

TEST_CASE("forecast: shadow casters are marked and get a ghost arc where they will park") {
  ContentDB db = waveContent();
  db.suns[0].type = SunPathType::Fixed;
  db.waveSets[0].waves[0].spawns = {{0, kEclipser, 1}, {1, kDart, 1}};
  Sim sim(waveConfig(db));
  World& w = sim.world();
  w.stardust = 1000.0f;
  sim.push(cmd::placeBuilding(0, 2, kPanel, 0.0f));
  sim.push(cmd::placeBuilding(0, 3, kPanel, 0.0f));
  sim.step();
  const std::vector<WaveForecast> f = forecast(w, 1);
  REQUIRE(f.size() == 1);
  CHECK(f[0].entries[0].shadowCaster);
  CHECK_FALSE(f[0].entries[1].shadowCaster);
  REQUIRE(f[0].eclipserGhostArcs.size() == 2);
  // The arc covers both panels (20 and 30 deg).
  CHECK(f[0].eclipserGhostArcs[0] < 20.0f * kDeg);
  CHECK(f[0].eclipserGhostArcs[1] > 30.0f * kDeg);

  // It does not lie: run the wave and compare with the real shadow at hover.
  sim.push(cmd::startWave(sim.tick()));
  bool compared = false;
  for (int i = 0; i < 3000 && !compared; ++i) {
    sim.step();
    for (uint32_t e = 0; e < w.enemies.count; ++e) {
      if (w.enemies.type[e] != kEclipser || eclipserState(w.enemies.flags[e]) != EclipserState::Hover) continue;
      sim.step();
      float angles[4];
      REQUIRE(shadeIntervalAngles(w, angles, 2) == 1);
      CHECK(angles[0] == doctest::Approx(f[0].eclipserGhostArcs[0]).epsilon(0.01));
      CHECK(angles[1] == doctest::Approx(f[0].eclipserGhostArcs[1]).epsilon(0.01));
      compared = true;
    }
  }
  CHECK(compared);
}

TEST_CASE("forecast: with a drifting sun the ghost arc is where the sun will be on arrival") {
  ContentDB db = waveContent();
  db.waveSets[0].waves[0].spawns = {{0, kEclipser, 1}};
  Sim sim(waveConfig(db));
  const std::vector<WaveForecast> f = forecast(sim.world(), 1);
  REQUIRE(f[0].eclipserGhostArcs.size() == 2);
  // (900 - 260) / 25 = 25.6 s at 1.5 deg/s = 38.4 deg; no panels: centred on the sun line.
  const float centre = 0.5f * (f[0].eclipserGhostArcs[0] + f[0].eclipserGhostArcs[1]);
  CHECK(centre == doctest::Approx(38.4f * kDeg).epsilon(0.01));
}

TEST_CASE("forecast: the strip matches the actual spawns for all 8 shipped waves") {
  ContentDB db = shipped();
  REQUIRE(db.waveSets.size() >= 1);
  const int planet = db.waveSets[0].planet;
  db.planets[static_cast<size_t>(planet)].hp = 1.0e6f;  // nobody defends; count what arrives
  SimConfig cfg;
  cfg.seed = 9;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();
  REQUIRE(w.waveCount() == 8);
  const float baseStrength = db.suns[static_cast<size_t>(db.waveSets[0].sun)].baseStrength;

  for (uint32_t wave = 0; wave < 8; ++wave) {
    REQUIRE(w.phase == Phase::Intermission);
    const std::vector<WaveForecast> strip = forecast(w, 4);
    REQUIRE(!strip.empty());
    const WaveForecast& next = strip[0];
    CHECK(next.wave == static_cast<int>(wave) + 1);
    CHECK(next.exact);
    std::map<std::pair<int, int>, int> promised;  // (lane, enemy) -> count
    int casters = 0;
    for (const ForecastEntry& e : next.entries) {
      promised[{e.lane, e.enemy}] += e.count;
      if (e.shadowCaster) casters += e.count;
      CHECK(e.laneBearing == db.planets[static_cast<size_t>(planet)].lanes[static_cast<size_t>(e.lane)].bearing);
    }
    CHECK(next.eclipserGhostArcs.size() == static_cast<size_t>(2 * casters));

    w.events.clear();
    const uint32_t spawnedBefore = w.enemiesSpawned;
    sim.push(cmd::startWave(sim.tick()));
    sim.step();
    CHECK(w.lightMult == next.lightMult);
    CHECK(w.beamStrength == doctest::Approx(baseStrength * next.lightMult));
    // Tally every unit as it appears: the pool only grows by spawns here until
    // the first landing, so read lane and type off each new arrival's event.
    std::map<std::pair<int, int>, int> arrived;
    float lastSpawnTime = 0.0f;
    uint32_t seen = w.enemiesSpawned;
    uint32_t guard = 0;
    while (w.phase == Phase::Wave && guard++ < 30000) {
      sim.step();
      if (w.enemiesSpawned != seen) {
        seen = w.enemiesSpawned;
        lastSpawnTime = w.waveTime;
      }
      for (const Event& e : w.events.items) {
        if (e.type == EventType::PlanetHit) arrived[{e.b, e.a}] += 1;
      }
      w.events.clear();
    }
    REQUIRE(w.phase != Phase::Wave);
    CHECK(arrived == promised);
    int total = 0;
    for (const auto& kv : promised) total += kv.second;
    CHECK(static_cast<int>(w.enemiesSpawned - spawnedBefore) == total);
    // Spread over the wave's spawnSeconds (BRIEF: 15 to 25 s), not all at once.
    const float spawnSeconds = db.waveSets[0].waves[wave].spawnSeconds;
    CHECK(lastSpawnTime <= spawnSeconds + 0.1f);
    if (total > 2) CHECK(lastSpawnTime > 0.5f * spawnSeconds);
  }
  CHECK(w.phase == Phase::Won);
  CHECK(forecast(w, 4).empty());
}

namespace {

// A scripted full run on the shipped content: build, turn, start every wave.
// Returns the final hash; fills the log and the tick count for the replay.
uint64_t scriptedRun(uint32_t seed, std::vector<Command>& log, uint32_t& ticks, Phase& end) {
  const ContentDB& db = shipped();
  SimConfig cfg;
  cfg.seed = seed;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();
  Rng script;
  script.seed(77);
  const int buildings = static_cast<int>(db.buildings.size());
  uint32_t guard = 0;
  while (w.phase != Phase::Won && w.phase != Phase::Lost && guard++ < 60000) {
    const uint32_t t = sim.tick();
    if (w.phase == Phase::Intermission) {
      // A few (sometimes invalid or unaffordable) orders, then go.
      for (int k = 0; k < 6; ++k) {
        sim.push(cmd::placeBuilding(t, static_cast<int32_t>(script.nextBelow(36)),
                                    static_cast<int32_t>(script.nextBelow(static_cast<uint32_t>(buildings))),
                                    script.range(-0.5f, 0.5f)));
      }
      if (script.nextBelow(3) == 0) sim.push(cmd::sellBuilding(t, static_cast<int32_t>(script.nextBelow(36))));
      sim.push(cmd::startWave(t + 5));
      for (int k = 0; k < 6; ++k) sim.step();
    } else {
      if (t % 45 == 0) sim.push(cmd::rotateInput(t, script.range(-1.0f, 1.0f)));
      if (t % 400 == 0) sim.push(cmd::setTimeScale(t, script.nextBelow(2) ? 0.75f : 1.0f, 0.3f));
      if (t % 700 == 0) sim.push(cmd::setTargeting(t, static_cast<int32_t>(script.nextBelow(36)),
                                                   static_cast<uint8_t>(script.nextBelow(3))));
      sim.step();
    }
    w.events.clear();
  }
  log = w.commandLog;
  ticks = sim.tick();
  end = w.phase;
  return sim.hash();
}

}  // namespace

TEST_CASE("replay: seed + command log reproduce the hash of a full scripted run") {
  std::vector<Command> log, log2;
  uint32_t ticks = 0, ticks2 = 0;
  Phase end = Phase::Intermission, end2 = Phase::Intermission;
  const uint64_t live = scriptedRun(1234, log, ticks, end);
  CHECK((end == Phase::Won || end == Phase::Lost));  // the run played to its end
  CHECK(ticks > 3000);
  CHECK(log.size() > 50);

  // The same script again: same hash.
  CHECK(scriptedRun(1234, log2, ticks2, end2) == live);
  CHECK(ticks2 == ticks);

  // Replay from seed + log only, all commands handed over up front.
  SimConfig cfg;
  cfg.seed = 1234;
  cfg.content = &shipped();
  cfg.waveSet = 0;
  CHECK(replayHash(cfg, log, ticks) == live);

  // Another seed diverges (Eclipser spawn jitter), and so does a changed log.
  cfg.seed = 1235;
  CHECK(replayHash(cfg, log, ticks) != live);
  cfg.seed = 1234;
  std::vector<Command> edited = log;
  edited.erase(edited.begin() + 2);
  CHECK(replayHash(cfg, edited, ticks) != live);
}

TEST_CASE("replay: hashes match tick by tick, not only at the end") {
  const ContentDB db = waveContent();
  Sim a(waveConfig(db, 5));
  a.world().recordCommands = true;
  a.push(cmd::placeBuilding(0, 3, kGatling, 0.0f));
  a.push(cmd::placeBuilding(0, 0, kPanel, 0.0f));
  a.push(cmd::startWave(3));
  a.push(cmd::rotateInput(40, 0.5f));
  std::vector<uint64_t> hashes;
  for (int i = 0; i < 400; ++i) {
    a.step();
    hashes.push_back(a.hash());
  }
  Sim b(waveConfig(db, 5));
  for (const Command& c : a.world().commandLog) b.push(c);
  for (int i = 0; i < 400; ++i) {
    b.step();
    REQUIRE(b.hash() == hashes[static_cast<size_t>(i)]);
  }
}
