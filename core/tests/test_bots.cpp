// Deterministic bot runs on the shipped content, as a sanity check of the M1
// numbers: a scripted, never-rotating build survives waves 1 to 3, and a run
// that never builds loses. Building ids are looked up at the edge, like a
// client would; the sim only sees indices.
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/waves.h"

using namespace orbitfall;

namespace {

struct Order {
  uint32_t wave;  // intermission before this 0-based wave
  int32_t slot;
  const char* building;
};

struct BotResult {
  Phase phase = Phase::Intermission;
  uint32_t wavesCleared = 0;
  float hp = 0.0f;
  float hpAfterWave[16] = {};
  uint32_t kills = 0, landed = 0, rejected = 0;
  uint64_t hash = 0;
};

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

// Places the orders in each intermission, starts the wave, never rotates.
// Stops after `maxWaves` waves or when the run ends.
BotResult runBot(const std::vector<Order>& orders, uint32_t maxWaves, uint32_t seed = 1) {
  const ContentDB& db = shipped();
  SimConfig cfg;
  cfg.seed = seed;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();
  BotResult r;
  uint32_t guard = 0;
  while (w.phase != Phase::Won && w.phase != Phase::Lost && guard++ < 200000) {
    if (w.phase == Phase::Intermission) {
      if (w.waveIndex >= maxWaves) break;
      for (const Order& o : orders) {
        if (o.wave != w.waveIndex) continue;
        const int building = db.building_index(o.building);
        REQUIRE(building != kNoIndex);
        sim.push(cmd::placeBuilding(sim.tick(), o.slot, building, 0.0f));
      }
      sim.push(cmd::startWave(sim.tick()));
    }
    sim.step();
    for (const Event& e : w.events.items) {
      if (e.type == EventType::EnemyKilled) ++r.kills;
      if (e.type == EventType::PlanetHit) ++r.landed;
      if (e.type == EventType::CommandRejected) ++r.rejected;
      if (e.type == EventType::WaveCleared && e.a < 16) r.hpAfterWave[e.a] = w.planetHp;
    }
    w.events.clear();
  }
  r.phase = w.phase;
  r.wavesCleared = w.waveIndex;
  r.hp = w.planetHp;
  r.hash = sim.hash();
  return r;
}

// One Gatling under each lane and panels ahead of the drifting sun; slot i is
// at 10 * i degrees, lanes come in at 30 and 170, the sun starts at 0.
const std::vector<Order>& starterBuild() {
  static const std::vector<Order> orders = {
      {0, 3, "gatling"}, {0, 5, "solar_panel"}, {0, 6, "solar_panel"},
      {1, 17, "gatling"}, {1, 7, "solar_panel"}, {1, 8, "solar_panel"},
      {2, 4, "gatling"}, {2, 16, "gatling"}, {2, 9, "solar_panel"}, {2, 10, "solar_panel"},
  };
  return orders;
}

}  // namespace

TEST_CASE("bot: a scripted placement survives waves 1 to 3") {
  const BotResult r = runBot(starterBuild(), 3);
  CHECK(r.phase == Phase::Intermission);
  CHECK(r.wavesCleared == 3);
  CHECK(r.hp > 0.5f * shipped().planets[0].hp);  // comfortably, not by a thread
  CHECK(r.rejected == 0);                         // every order was affordable
  CHECK(r.kills > 0);
  // Deterministic: the same run again gives the same state.
  CHECK(runBot(starterBuild(), 3).hash == r.hash);
}

TEST_CASE("bot: never building loses, and each wave hurts as much as its spawns say") {
  const ContentDB& db = shipped();
  const BotResult r = runBot({}, 99);
  CHECK(r.phase == Phase::Lost);
  CHECK(r.hp == 0.0f);
  CHECK(r.kills == 0);
  CHECK(r.wavesCleared < db.waveSets[0].waves.size());
  REQUIRE(r.wavesCleared >= 1);
  // Undefended, a wave costs exactly the land damage of everything in it.
  float hp = db.planets[static_cast<size_t>(db.waveSets[0].planet)].hp;
  for (uint32_t wave = 0; wave < r.wavesCleared; ++wave) {
    for (const SpawnDef& s : db.waveSets[0].waves[wave].spawns) {
      const EnemyDef& def = db.enemies[static_cast<size_t>(s.enemy)];
      hp -= static_cast<float>(s.count) * def.land_damage(def.layers);
    }
    CHECK(r.hpAfterWave[wave] == doctest::Approx(hp));
  }
}
