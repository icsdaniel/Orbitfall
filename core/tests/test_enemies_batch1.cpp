// M2.5 enemies batch 1: a behavior test per mechanic.
//
// The two new mechanics (Regrow, Splitter) are tested on tiny worlds with
// hand-built definitions, so the numbers here do not move with content. The
// shipped enemies (Brick, Phantom, Bubble, Ember, Spore, Splitter) are then
// checked against real towers in sandbox runs on the shipped content: each
// one is harder for the tower its trait resists and easier for its answer.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "bots/bots.h"
#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

constexpr float kTick = 1.0f / 30.0f;

std::vector<Event> eventsOf(const World& w, EventType type) {
  std::vector<Event> out;
  for (const Event& e : w.events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

void stepSeconds(Sim& sim, float seconds) {
  const int ticks = static_cast<int>(std::lround(seconds / kTick));
  for (int t = 0; t < ticks; ++t) sim.step();
}

// Enemy `i`, parked at (x, y) and standing still.
uint32_t park(World& w, uint32_t i, float x, float y) {
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

// tinyContent() plus a three-layer regrowing chain (spore3 -> spore2 -> spore1,
// regrowSeconds 1) and a Splitter that splits into three reds 10 units apart.
struct Batch1 {
  ContentDB db;
  int spore3 = kNoIndex, spore2 = kNoIndex, spore1 = kNoIndex, splitter = kNoIndex;
};

Batch1 batch1Content() {
  Batch1 b;
  b.db = tinyContent();
  ContentDB& db = b.db;
  auto add = [&db](EnemyDef e) {
    db.enemies.push_back(e);
    const int i = static_cast<int>(db.enemies.size()) - 1;
    db.enemyIds[e.id] = i;
    return i;
  };
  EnemyDef s = db.enemies[kRed];
  s.traits = kTraitRegrow;
  s.regrowSeconds = 1.0f;
  s.id = "spore1";
  b.spore1 = add(s);
  s.id = "spore2";
  s.layers = 2;
  s.hp = 2.0f;
  s.popsInto = b.spore1;
  s.traits |= kTraitLayered;
  b.spore2 = add(s);
  s.id = "spore3";
  s.layers = 3;
  s.hp = 3.0f;
  s.popsInto = b.spore2;
  b.spore3 = add(s);

  EnemyDef sp = db.enemies[kRed];
  sp.id = "splitter";
  sp.hp = sp.hpPerLayer = 3.0f;
  sp.size = 10.0f;
  sp.traits = kTraitSplitter;
  sp.splitInto = kRed;
  sp.splitCount = 3;
  sp.splitSpread = 10.0f;
  b.splitter = add(sp);
  return b;
}

}  // namespace

// --- Regrow (Spore) ---

TEST_CASE("M2.5 Regrow: a popped spore left alone regains a layer after regrowSeconds") {
  const Batch1 b = batch1Content();
  Sim sim(sandboxConfig(b.db));
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, b.spore3, 0), 600.0f, 0.0f);
  CHECK((w.enemies.traits[i] & kTraitRegrow) != 0);
  CHECK(w.enemies.spawnType[i] == b.spore3);

  damageEnemy(w, i, 1.0f);
  REQUIRE(w.enemies.type[i] == b.spore2);
  w.events.clear();
  stepSeconds(sim, 0.9f);
  CHECK(w.enemies.type[i] == b.spore2);  // not yet
  stepSeconds(sim, 0.2f);
  CHECK(w.enemies.type[i] == b.spore3);  // regrown one layer, full layer hit points
  CHECK(w.enemies.hp[i] == doctest::Approx(1.0f));
  const std::vector<Event> regrown = eventsOf(w, EventType::LayerRegrown);
  REQUIRE(regrown.size() == 1);
  CHECK(regrown[0].a == b.spore2);
  CHECK(regrown[0].b == b.spore3);
}

TEST_CASE("M2.5 Regrow: it climbs one layer per regrowSeconds and never above its spawn layer") {
  const Batch1 b = batch1Content();
  SUBCASE("spawned as the top layer") {
    Sim sim(sandboxConfig(b.db));
    World& w = sim.world();
    const uint32_t i = park(w, spawnEnemy(w, b.spore3, 0), 600.0f, 0.0f);
    damageEnemy(w, i, 2.0f);  // two layers off
    REQUIRE(w.enemies.type[i] == b.spore1);
    stepSeconds(sim, 1.05f);
    CHECK(w.enemies.type[i] == b.spore2);
    stepSeconds(sim, 1.0f);
    CHECK(w.enemies.type[i] == b.spore3);
    stepSeconds(sim, 5.0f);
    CHECK(w.enemies.type[i] == b.spore3);  // capped
  }
  SUBCASE("spawned as a middle layer") {
    Sim sim(sandboxConfig(b.db));
    World& w = sim.world();
    const uint32_t i = park(w, spawnEnemy(w, b.spore2, 0), 600.0f, 0.0f);
    damageEnemy(w, i, 1.0f);
    REQUIRE(w.enemies.type[i] == b.spore1);
    stepSeconds(sim, 5.0f);
    CHECK(w.enemies.type[i] == b.spore2);  // back to where it started, no higher
    CHECK(eventsOf(w, EventType::LayerRegrown).size() == 1);
  }
}

TEST_CASE("M2.5 Regrow: every hit restarts the timer, so steady fire keeps it down") {
  const Batch1 b = batch1Content();
  Sim sim(sandboxConfig(b.db));
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, b.spore3, 0), 600.0f, 0.0f);
  damageEnemy(w, i, 1.0f);
  REQUIRE(w.enemies.type[i] == b.spore2);
  for (int k = 0; k < 8; ++k) {  // a chip of damage every 0.5 s for 4 s
    stepSeconds(sim, 0.5f);
    damageEnemy(w, i, 0.05f);
  }
  CHECK(w.enemies.type[i] == b.spore2);
  CHECK(eventsOf(w, EventType::LayerRegrown).empty());
  stepSeconds(sim, 1.1f);  // left alone
  CHECK(w.enemies.type[i] == b.spore3);
}

TEST_CASE("M2.5 Regrow: a Gatling (6 shots/s) kills a spore, a 0.5 shots/s tower cannot") {
  Batch1 b = batch1Content();
  BuildingDef slow = b.db.buildings[kGatling];
  slow.id = "slow";
  slow.fireRate = 0.5f;  // one shot per 2 s: the spore regrows (1 s) between shots
  b.db.buildings.push_back(slow);
  const int slowTower = static_cast<int>(b.db.buildings.size()) - 1;
  b.db.buildingIds[slow.id] = slowTower;

  auto alive = [&b](int tower) {
    Sim sim(sandboxConfig(b.db));
    sim.world().stardust = 1000.0f;
    sim.push(cmd::placeBuilding(sim.tick(), 0, tower, 0.0f));  // slot 0: world angle 0
    sim.step();
    park(sim.world(), spawnEnemy(sim.world(), b.spore3, 0), 250.0f, 0.0f);
    stepSeconds(sim, 12.0f);
    return sim.world().enemies.count;
  };
  CHECK(alive(kGatling) == 0);
  CHECK(alive(slowTower) == 1);
}

// --- Splitter ---

TEST_CASE("M2.5 Splitter: killed by damage it splits into splitCount children at its spot") {
  const Batch1 b = batch1Content();
  Sim sim(sandboxConfig(b.db));
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, b.splitter, 1), 300.0f, 0.0f);
  w.events.clear();
  CHECK(damageEnemy(w, i, 3.0f));
  sim.step();
  REQUIRE(w.enemies.count == 3);
  std::vector<float> ys;
  for (uint32_t k = 0; k < w.enemies.count; ++k) {
    CHECK(w.enemies.type[k] == kRed);
    CHECK(w.enemies.lane[k] == 1);  // the parent's lane
    // Heading for the planet centre at the child's own speed; spawned after
    // movement, so it has not moved yet.
    const float x = w.enemies.posX[k], y = w.enemies.posY[k];
    const float vx = w.enemies.velX[k], vy = w.enemies.velY[k];
    CHECK(std::sqrt(vx * vx + vy * vy) == doctest::Approx(40.0f));
    CHECK(x * vy - y * vx == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(x * vx + y * vy < 0.0f);
    CHECK(x == doctest::Approx(300.0f));
    ys.push_back(w.enemies.posY[k]);
  }
  std::sort(ys.begin(), ys.end());
  CHECK(ys[0] == doctest::Approx(-10.0f));  // spread sideways over +-splitSpread
  CHECK(ys[1] == doctest::Approx(0.0f));
  CHECK(ys[2] == doctest::Approx(10.0f));
  const std::vector<Event> split = eventsOf(w, EventType::EnemySplit);
  REQUIRE(split.size() == 1);
  CHECK(split[0].a == b.splitter);
  CHECK(split[0].b == kRed);
  CHECK(split[0].value == doctest::Approx(3.0f));
  CHECK(eventsOf(w, EventType::EnemyKilled).size() == 1);  // the parent's bounty
}

TEST_CASE("M2.5 Splitter: landing does not split") {
  const Batch1 b = batch1Content();
  Sim sim(sandboxConfig(b.db));
  World& w = sim.world();
  const uint32_t i = spawnEnemy(w, b.splitter, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = 105.0f;  // just above the surface, flying in
  w.enemies.posY[i] = 0.0f;
  w.enemies.velX[i] = -40.0f;
  w.enemies.velY[i] = 0.0f;
  const float hp0 = w.planetHp;
  stepSeconds(sim, 1.0f);
  CHECK(w.enemies.count == 0);
  CHECK(w.planetHp < hp0);
  CHECK(eventsOf(w, EventType::EnemySplit).empty());
}

TEST_CASE("M2.5 Splitter: a Gatling kills the parent and then the children (four kills)") {
  const Batch1 b = batch1Content();
  Sim sim(sandboxConfig(b.db));
  sim.world().stardust = 1000.0f;
  sim.push(cmd::placeBuilding(sim.tick(), 0, kGatling, 0.0f));
  sim.step();
  park(sim.world(), spawnEnemy(sim.world(), b.splitter, 0), 250.0f, 0.0f);
  int kills = 0, splits = 0;
  for (int t = 0; t < 300 && (t == 0 || sim.world().enemies.count > 0); ++t) {
    sim.step();
    kills += static_cast<int>(eventsOf(sim.world(), EventType::EnemyKilled).size());
    splits += static_cast<int>(eventsOf(sim.world(), EventType::EnemySplit).size());
    sim.world().events.clear();
  }
  CHECK(sim.world().enemies.count == 0);
  CHECK(splits == 1);
  CHECK(kills == 4);
}

TEST_CASE("M2.5 Splitter: a full pool drops children, and splitting does not allocate") {
  const Batch1 b = batch1Content();
  SimConfig cfg = sandboxConfig(b.db);
  cfg.enemyCapacity = 2;
  Sim sim(cfg);
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, b.splitter, 0), 300.0f, 0.0f);
  damageEnemy(w, i, 3.0f);
  const uint64_t before = allocationCount();
  w.events.clear();
  sim.step();
  CHECK(allocationCount() == before);
  CHECK(w.enemies.count == 1);  // the dead parent held a slot while the children spawned
  CHECK(w.enemies.overflowCount == 2);
  const std::vector<Event> split = eventsOf(w, EventType::EnemySplit);
  REQUIRE(split.size() == 1);
  CHECK(split[0].value == doctest::Approx(1.0f));
}

TEST_CASE("M2.5 Splitter and Regrow are deterministic") {
  const Batch1 b = batch1Content();
  auto run = [&b]() {
    Sim sim(sandboxConfig(b.db, 11));
    sim.world().stardust = 1000.0f;
    sim.push(cmd::placeBuilding(sim.tick(), 0, kGatling, 0.0f));
    sim.push(cmd::placeBuilding(sim.tick(), 3, kCannon, 0.0f));
    sim.step();
    for (int k = 0; k < 6; ++k) {
      spawnEnemy(sim.world(), k % 2 ? b.splitter : b.spore3, 0);
      spawnEnemy(sim.world(), b.splitter, 1);
    }
    stepSeconds(sim, 30.0f);
    return sim.hash();
  };
  CHECK(run() == run());
}

// --- The shipped enemies against real towers (sandbox runs on content/) ---

namespace {

constexpr int kTowerSlot = 18;  // world angle 180 deg on the 36-slot medium planet

ContentDB shipped() {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
  return db;
}

int enemy(const ContentDB& db, const char* id) {
  const int i = db.enemy_index(id);
  REQUIRE_MESSAGE(i != kNoIndex, id);
  return i;
}

// A copy of shipped enemy `id` that never dies, with `traits` in place of its own.
int hugeCopy(ContentDB& db, const char* id, uint32_t traits) {
  EnemyDef e = db.enemies[static_cast<size_t>(enemy(db, id))];
  e.id = std::string(id) + "_target" + std::to_string(db.enemies.size());
  e.layers = 1;
  e.hp = e.hpPerLayer = 1000.0f;
  e.popsInto = kNoIndex;
  e.splitInto = kNoIndex;
  e.splitCount = 0;
  e.regrowSeconds = 0.0f;
  e.traits = traits;
  db.enemies.push_back(e);
  db.enemyIds[e.id] = static_cast<int>(db.enemies.size()) - 1;
  return static_cast<int>(db.enemies.size()) - 1;
}

SimConfig shippedSandbox(const ContentDB& db) {
  SimConfig cfg;
  cfg.seed = 5;
  cfg.content = &db;
  cfg.waveSet = kNoIndex;
  cfg.planet = db.planet_index("medium");
  cfg.sun = db.sun_index("default");
  cfg.enemyCapacity = 256;
  cfg.projectileCapacity = 1024;
  return cfg;
}

// Puts `type` (standing still) 120 units above slot kTowerSlot.
uint32_t inFront(World& w, int type) {
  const float a = 2.0f * kPi * static_cast<float>(kTowerSlot) / static_cast<float>(w.planet->slots);
  const float r = w.planet->radius + 120.0f;
  return park(w, spawnEnemy(w, type, 0), r * std::cos(a), r * std::sin(a));
}

// Hit points (plus shield) `towers` take off one `type` in `seconds`; the
// first tower stands on kTowerSlot, the others on the next slots.
float dealt(const ContentDB& db, const std::vector<const char*>& towers, int type, float seconds) {
  Sim sim(shippedSandbox(db));
  sim.world().stardust = 1.0e6f;
  for (size_t k = 0; k < towers.size(); ++k) {
    const int slot = kTowerSlot + (k == 0 ? 0 : static_cast<int>(k) == 1 ? 1 : -1);
    sim.push(cmd::placeBuilding(sim.tick(), slot, db.building_index(towers[k]), 0.0f));
    sim.step();
    REQUIRE(sim.world().site(static_cast<uint32_t>(slot)).type == db.building_index(towers[k]));
  }
  const uint32_t i = inFront(sim.world(), type);
  const float hp0 = sim.world().enemies.hp[i], shield0 = sim.world().enemies.shield[i];
  stepSeconds(sim, seconds);
  return (hp0 - sim.world().enemies.hp[i]) + (shield0 - sim.world().enemies.shield[i]);
}

}  // namespace

TEST_CASE("M2.5 Brick (Armored): the Gatling deals half to it, Flak deals full") {
  ContentDB db = shipped();
  const uint32_t own = db.enemies[static_cast<size_t>(enemy(db, "brick"))].traits;
  CHECK((own & kTraitArmored) != 0);
  CHECK(db.enemies[static_cast<size_t>(enemy(db, "brick"))].visual.shape == Shape::Square);
  const int brick = hugeCopy(db, "brick", own), plain = hugeCopy(db, "brick", 0);
  const float gatling = dealt(db, {"gatling"}, brick, 6.0f) / dealt(db, {"gatling"}, plain, 6.0f);
  const float flak = dealt(db, {"flak"}, brick, 6.0f) / dealt(db, {"flak"}, plain, 6.0f);
  CHECK(gatling == doctest::Approx(0.5f).epsilon(0.02));
  CHECK(flak == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("M2.5 Phantom (Camo): every other Gatling shot misses unless a Radar stands beside it") {
  ContentDB db = shipped();
  const uint32_t own = db.enemies[static_cast<size_t>(enemy(db, "phantom"))].traits;
  CHECK((own & kTraitCamo) != 0);
  const int phantom = hugeCopy(db, "phantom", own), plain = hugeCopy(db, "phantom", 0);
  const float bare = dealt(db, {"gatling"}, phantom, 6.0f) / dealt(db, {"gatling"}, plain, 6.0f);
  const float radar =
      dealt(db, {"gatling", "radar"}, phantom, 6.0f) / dealt(db, {"gatling", "radar"}, plain, 6.0f);
  CHECK(bare <= 0.5f + 1e-3f);
  CHECK(bare >= 0.4f);
  CHECK(radar == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("M2.5 Bubble (Shielded): its shield soaks the first hits, then one more pops it") {
  const ContentDB db = shipped();
  const int bubble = enemy(db, "bubble");
  const EnemyDef& def = db.enemies[static_cast<size_t>(bubble)];
  REQUIRE(def.shield > 0.0f);
  CHECK((def.traits & kTraitShielded) != 0);
  Sim sim(shippedSandbox(db));
  World& w = sim.world();
  const uint32_t i = inFront(w, bubble);
  int hits = 0;
  while (!damageEnemy(w, i, 1.0f, DamageType::Kinetic) && hits < 100) ++hits;
  // A plain one-layer unit with the same hit points dies on the first hit.
  CHECK(hits + 1 == static_cast<int>(std::lround(def.shield + def.hp)));
  // The Amplifier's shield damage strips it faster (fewer Gatling seconds).
  ContentDB big = shipped();
  EnemyDef copy = big.enemies[static_cast<size_t>(enemy(big, "bubble"))];
  copy.id = "bubble_wall";
  copy.shield = 1000.0f;
  big.enemies.push_back(copy);
  big.enemyIds[copy.id] = static_cast<int>(big.enemies.size()) - 1;
  const int wall = static_cast<int>(big.enemies.size()) - 1;
  CHECK(dealt(big, {"gatling", "amplifier"}, wall, 6.0f) > 1.5f * dealt(big, {"gatling", "radar"}, wall, 6.0f));
}

TEST_CASE("M2.5 Ember (Fast, Heat-resistant): the Sun Lens deals half, Cryo more, and it outruns drifters") {
  ContentDB db = shipped();
  const EnemyDef& def = db.enemies[static_cast<size_t>(enemy(db, "ember"))];
  const uint32_t own = def.traits;
  CHECK((own & kTraitHeatResistant) != 0);
  CHECK((own & kTraitFast) != 0);
  CHECK(def.visual.shape == Shape::Triangle);  // shape = movement class: fast
  CHECK(def.speed > 2.0f * db.enemies[static_cast<size_t>(enemy(db, "drifter_red"))].speed);
  CHECK(typeMultiplier(db, def, DamageType::Energy) == doctest::Approx(0.5f));
  CHECK(typeMultiplier(db, def, DamageType::Cryo) > 1.0f);
  const int ember = hugeCopy(db, "ember", own), plain = hugeCopy(db, "ember", 0);
  const float lens = dealt(db, {"sun_lens"}, ember, 6.0f) / dealt(db, {"sun_lens"}, plain, 6.0f);
  const float gatling = dealt(db, {"gatling"}, ember, 6.0f) / dealt(db, {"gatling"}, plain, 6.0f);
  CHECK(lens == doctest::Approx(0.5f).epsilon(0.02));
  CHECK(gatling == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("M2.5 Spore (Regrow): a shipped spore left alone after a pop grows its layer back") {
  const ContentDB db = shipped();
  const int spore = enemy(db, "spore");
  const EnemyDef& def = db.enemies[static_cast<size_t>(spore)];
  REQUIRE(def.regrowSeconds > 0.0f);
  REQUIRE(def.popsInto != kNoIndex);
  Sim sim(shippedSandbox(db));
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, spore, 0), 700.0f, 0.0f);
  damageEnemy(w, i, def.hpPerLayer);
  CHECK(w.enemies.type[i] == def.popsInto);
  stepSeconds(sim, def.regrowSeconds + 0.1f);
  CHECK(w.enemies.type[i] == spore);
}

TEST_CASE("M2.5 Splitter: the shipped splitter splits into 2 to 4 children of a real enemy") {
  const ContentDB db = shipped();
  const int splitter = enemy(db, "splitter");
  const EnemyDef& def = db.enemies[static_cast<size_t>(splitter)];
  CHECK(def.splitCount >= 2);
  CHECK(def.splitCount <= 4);
  REQUIRE(def.splitInto != kNoIndex);
  Sim sim(shippedSandbox(db));
  World& w = sim.world();
  const uint32_t i = park(w, spawnEnemy(w, splitter, 0), 700.0f, 0.0f);
  CHECK(damageEnemy(w, i, 1000.0f));
  sim.step();
  CHECK(w.enemies.count == static_cast<uint32_t>(def.splitCount));
  for (uint32_t k = 0; k < w.enemies.count; ++k) CHECK(w.enemies.type[k] == def.splitInto);
}

// --- The bestiary demo wave set with the strategy bots ---

TEST_CASE("M2.5 m2_bestiary: the sensible bot wins it, never-build loses it") {
  const ContentDB db = shipped();
  orbitfall_bots::RunOptions options;
  options.waveSet = db.wave_set_index("m2_bestiary");
  REQUIRE(options.waveSet != kNoIndex);
  for (uint32_t seed = 1; seed <= 3; ++seed) {
    options.seed = seed;
    CAPTURE(seed);
    const orbitfall_bots::RunResult never = orbitfall_bots::play(db, orbitfall_bots::Strategy::Never, options);
    const orbitfall_bots::RunResult sensible =
        orbitfall_bots::play(db, orbitfall_bots::Strategy::Sensible, options);
    MESSAGE("seed " << seed << ": never-build " << never.wavesCleared << " waves; sensible "
                    << std::string(sensible.phase == Phase::Won ? "won" : "lost") << " with HP " << sensible.hp);
    CHECK(never.phase == Phase::Lost);
    CHECK(sensible.phase == Phase::Won);
  }
}
