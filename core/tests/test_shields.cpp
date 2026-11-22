// M2.10 falling objects and shields on tiny worlds (PLAN §5.4b, M2.10 AC):
// absorption drains the shield, unshielded hits cost planet HP only, offline
// shields recharge on schedule and 2x faster while lit, the warning comes 3 s
// before impact, Bombers drop bombs, volleys spawn from wave data, determinism.
#include <cmath>
#include <vector>

#include "doctest/doctest.h"
#include "sim/render_snapshot.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "systems/falling.h"
#include "systems/waves.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

enum TinyFalling { kMeteor = 0, kBomb = 1 };
constexpr int kShield = 3;   // building index after tinyContent()'s three
constexpr int kBomber = 5;   // enemy index after tinyContent()'s five

// tinyContent() plus a shield, a meteor, a bomb and a Bomber.
ContentDB shieldContent() {
  ContentDB db = tinyContent();
  db.rules.lightChargeMult = 2.0f;
  db.rules.fallingWarnSeconds = 3.0f;

  BuildingDef s;
  s.id = "shield";
  s.kind = BuildingKind::Shield;
  s.cost = 50.0f;
  s.shield.width = 60.0f * kDeg;
  s.shield.height = 40.0f;
  s.shield.energy = 20.0f;
  s.shield.rechargeSeconds = 10.0f;
  s.shield.regenPerSec = 0.0f;
  s.shield.upkeepPerSec = 0.0f;
  s.visual.shape = Shape::Hexagon;
  db.buildings.push_back(s);
  db.buildingIds["shield"] = kShield;

  FallingDef meteor;
  meteor.id = "meteor";
  meteor.damage = 4.0f;
  meteor.speed = 60.0f;
  meteor.size = 6.0f;
  meteor.spawnRadius = 400.0f;
  meteor.visual.shape = Shape::Star;
  db.falling.push_back(meteor);
  FallingDef bomb = meteor;
  bomb.id = "bomb";
  bomb.damage = 5.0f;
  bomb.speed = 40.0f;
  bomb.spawnRadius = 0.0f;
  db.falling.push_back(bomb);
  db.fallingIds["meteor"] = kMeteor;
  db.fallingIds["bomb"] = kBomb;

  EnemyDef bomber = db.enemies[kRed];
  bomber.id = "bomber";
  bomber.behavior = EnemyBehavior::Bomber;
  bomber.shape = Shape::Hexagon;
  bomber.hp = bomber.hpPerLayer = 12.0f;
  bomber.speed = 45.0f;
  bomber.size = 10.0f;
  bomber.flatLandDamage = true;
  bomber.landDamage = 4.0f;
  bomber.hasBomber = true;
  bomber.bomber = {260.0f, 12.0f * kDeg, 2.5f, 4, kBomb};
  db.enemies.push_back(bomber);
  db.enemyIds["bomber"] = kBomber;
  return db;
}

// Sandbox sim (always in a wave) with a fixed sun at 0 degrees, so slot 0 is lit
// and slot 18 is on the night side for the whole test.
struct Fixture {
  ContentDB db = shieldContent();
  Sim sim;
  std::vector<Event> events;
  explicit Fixture(bool fixedSun = true) {
    if (fixedSun) db.suns[0].type = SunPathType::Fixed;
    sim.reset(sandboxConfig(db));
    sim.world().stardust = 10000.0f;
  }
  World& w() { return sim.world(); }
  void step(int n = 1) {
    for (int i = 0; i < n; ++i) {
      sim.step();
      for (const Event& e : w().events.items) events.push_back(e);
      w().events.clear();
    }
  }
  void place(int slot, int building) {
    sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
    step();
    REQUIRE(w().slots[static_cast<size_t>(slot)].type == building);
  }
  int count(EventType type) const {
    int n = 0;
    for (const Event& e : events) n += e.type == type ? 1 : 0;
    return n;
  }
  const Event* first(EventType type) const {
    for (const Event& e : events)
      if (e.type == type) return &e;
    return nullptr;
  }
  // Steps until no falling object is left (or `limit` ticks).
  void fallAll(int limit = 3000) {
    for (int i = 0; i < limit && w().falling.count > 0; ++i) step();
    REQUIRE(w().falling.count == 0);
  }
};

}  // namespace

TEST_CASE("M2.10 a meteor crossing a shielded arc is absorbed and drains the shield") {
  Fixture f;
  f.place(0, kShield);  // world angle 0 at rotation 0
  const float hp = f.w().planetHp;
  CHECK(f.w().slots[0].energy == doctest::Approx(20.0f));
  REQUIRE(spawnFalling(f.w(), kMeteor, 20.0f * kDeg, 400.0f) != kInvalidIndex);  // inside +-30
  f.fallAll();
  CHECK(f.count(EventType::ShieldAbsorbed) == 1);
  CHECK(f.count(EventType::FallingImpact) == 0);
  CHECK(f.w().planetHp == doctest::Approx(hp));
  CHECK(f.w().slots[0].energy == doctest::Approx(16.0f));  // drained by the meteor's damage
  const Event* e = f.first(EventType::ShieldAbsorbed);
  REQUIRE(e != nullptr);
  CHECK(e->a == 0);
  CHECK(e->b == kMeteor);
  CHECK(e->value == doctest::Approx(16.0f));
  // It was caught at the dome (R + height), not at the surface.
  CHECK(std::sqrt(e->x * e->x + e->y * e->y) == doctest::Approx(140.0f).epsilon(0.02));
}

TEST_CASE("M2.10 an unshielded hit damages planet HP only, never a building") {
  Fixture f;
  f.place(0, kShield);
  f.place(18, kPanel);   // right under the meteor
  f.place(19, kGatling);
  const std::vector<BuildingSlot> before = f.w().slots;
  const float hp = f.w().planetHp;
  spawnFalling(f.w(), kMeteor, 180.0f * kDeg, 400.0f);  // night side, outside the shield's arc
  spawnFalling(f.w(), kMeteor, 35.0f * kDeg, 400.0f);   // just outside +-30 of the shield
  f.fallAll();
  CHECK(f.count(EventType::ShieldAbsorbed) == 0);
  CHECK(f.count(EventType::FallingImpact) == 2);
  CHECK(f.w().planetHp == doctest::Approx(hp - 8.0f));
  CHECK(f.w().slots[0].energy == doctest::Approx(20.0f));
  for (size_t s = 0; s < before.size(); ++s) {
    CAPTURE(s);
    CHECK(f.w().slots[s].type == before[s].type);  // nothing destroyed, nothing changed
  }
  const Event* hit = f.first(EventType::FallingImpact);
  REQUIRE(hit != nullptr);
  CHECK(hit->value == doctest::Approx(4.0f));
  CHECK(std::sqrt(hit->x * hit->x + hit->y * hit->y) == doctest::Approx(100.0f));
  CHECK(f.count(EventType::PlanetHit) == 0);  // PlanetHit stays the enemy landing event
}

TEST_CASE("M2.10 a falling object can bring the planet down") {
  Fixture f;
  f.w().planetHp = 3.0f;
  spawnFalling(f.w(), kMeteor, 90.0f * kDeg, 400.0f);
  for (int i = 0; i < 400 && f.w().phase != Phase::Lost; ++i) f.step();
  CHECK(f.w().phase == Phase::Lost);
  CHECK(f.w().planetHp == doctest::Approx(0.0f));
  CHECK(f.count(EventType::GameOver) == 1);
}

namespace {
// Drains the shield on `slot` (at world angle `angle`) to zero; returns the
// ticks until it is back online.
int ticksToRecharge(Fixture& f, int slot, float angle) {
  for (int k = 0; k < 5; ++k) spawnFalling(f.w(), kMeteor, angle, 400.0f + 3.0f * static_cast<float>(k));
  for (int i = 0; i < 2000 && f.count(EventType::ShieldOffline) == 0; ++i) f.step();
  REQUIRE(f.count(EventType::ShieldOffline) == 1);
  CHECK((f.w().slots[static_cast<size_t>(slot)].flags & kSlotShieldOffline) != 0);
  CHECK(f.w().slots[static_cast<size_t>(slot)].energy == doctest::Approx(0.0f));
  int ticks = 0;
  while (f.count(EventType::ShieldOnline) == 0 && ticks < 2000) {
    f.step();
    ++ticks;
  }
  CHECK((f.w().slots[static_cast<size_t>(slot)].flags & kSlotShieldOffline) == 0);
  CHECK(f.w().slots[static_cast<size_t>(slot)].energy == doctest::Approx(20.0f));  // back full
  return ticks;
}
}  // namespace

TEST_CASE("M2.10 an offline shield recharges on schedule, about 2x faster while its slot is lit") {
  Fixture dark;
  dark.place(18, kShield);  // night side
  REQUIRE((dark.w().slots[18].flags & kSlotLit) == 0);
  const int darkTicks = ticksToRecharge(dark, 18, 180.0f * kDeg);
  // The meteors that arrive while it is offline get through.
  CHECK(dark.count(EventType::FallingImpact) == 0);
  CHECK(darkTicks == doctest::Approx(10.0f * 30.0f).epsilon(0.02));  // rechargeSeconds at 1x

  Fixture lit;
  lit.place(0, kShield);  // under the sun
  REQUIRE((lit.w().slots[0].flags & (kSlotLit | kSlotShaded)) == kSlotLit);
  const int litTicks = ticksToRecharge(lit, 0, 0.0f);
  CHECK(litTicks == doctest::Approx(5.0f * 30.0f).epsilon(0.02));
  CHECK(static_cast<float>(darkTicks) / static_cast<float>(litTicks) == doctest::Approx(2.0f).epsilon(0.03));
}

TEST_CASE("M2.10 an offline shield lets objects through until it is back") {
  Fixture f;
  f.place(18, kShield);
  const float hp = f.w().planetHp;
  for (int k = 0; k < 6; ++k) spawnFalling(f.w(), kMeteor, 180.0f * kDeg, 400.0f + 3.0f * static_cast<float>(k));
  f.fallAll();
  CHECK(f.count(EventType::ShieldAbsorbed) == 5);  // 20 energy / 4 damage
  CHECK(f.count(EventType::ShieldOffline) == 1);
  CHECK(f.count(EventType::FallingImpact) == 1);
  CHECK(f.w().planetHp == doctest::Approx(hp - 4.0f));
}

TEST_CASE("M2.10 the warning comes 3 seconds before impact") {
  Fixture f;
  REQUIRE(spawnFalling(f.w(), kMeteor, 45.0f * kDeg, 400.0f) != kInvalidIndex);  // 5 s fall
  uint32_t warnedAt = 0, hitAt = 0;
  for (int i = 0; i < 400 && hitAt == 0; ++i) {
    f.step();
    for (const Event& e : f.events) {
      if (e.type == EventType::FallingWarning && warnedAt == 0) {
        warnedAt = e.tick;
        CHECK(e.value <= 3.0f);
        CHECK(e.value > 3.0f - 2.0f * kFixedDt);
        CHECK(e.a == kMeteor);
        CHECK(e.b == -1);  // no shield covers it
        CHECK(std::atan2(e.y, e.x) == doctest::Approx(45.0f * kDeg));
      }
      if (e.type == EventType::FallingImpact) hitAt = e.tick;
    }
  }
  REQUIRE(warnedAt > 0);
  REQUIRE(hitAt > warnedAt);
  const float lead = static_cast<float>(hitAt - warnedAt) * kFixedDt;
  CHECK(lead == doctest::Approx(3.0f).epsilon(0.02));
  CHECK(f.count(EventType::FallingWarning) == 1);  // once per object
}

TEST_CASE("M2.10 the warning names the shield that covers the bearing") {
  Fixture f;
  f.place(9, kShield);  // local 90 degrees
  spawnFalling(f.w(), kMeteor, 80.0f * kDeg, 400.0f);
  f.fallAll();
  const Event* warn = f.first(EventType::FallingWarning);
  REQUIRE(warn != nullptr);
  CHECK(warn->b == 9);
  CHECK(shieldCovering(f.w(), 80.0f * kDeg) == 9);
  CHECK(shieldCovering(f.w(), 200.0f * kDeg) == -1);
}

TEST_CASE("M2.10 rotating the planet brings a shield under a falling object") {
  Fixture f;
  f.place(0, kShield);
  spawnFalling(f.w(), kMeteor, 90.0f * kDeg, 400.0f);
  // Turn so that slot 0 faces 90 degrees before the meteor reaches the dome.
  for (int i = 0; i < 300 && f.w().falling.count > 0; ++i) {
    const float err = wrapPi(90.0f * kDeg - f.w().planetRotation);
    f.sim.push(cmd::rotateInput(f.sim.tick(), clampf(err / (20.0f * kDeg), -1.0f, 1.0f)));
    f.step();
  }
  CHECK(f.count(EventType::ShieldAbsorbed) == 1);
  CHECK(f.count(EventType::FallingImpact) == 0);
}

TEST_CASE("M2.10 shields regenerate while online and charge upkeep only when asked") {
  Fixture f;
  f.db.buildings[kShield].shield.regenPerSec = 1.0f;
  f.place(18, kShield);  // dark: 1x
  f.w().slots[18].energy = 10.0f;
  const float stardust = f.w().stardust;
  f.step(60);
  CHECK(f.w().slots[18].energy == doctest::Approx(12.0f).epsilon(0.01));
  CHECK(f.w().stardust == doctest::Approx(stardust));  // upkeepPerSec 0: free
  f.place(0, kShield);   // lit: 2x
  f.w().slots[0].energy = 10.0f;
  f.step(30);
  CHECK(f.w().slots[0].energy == doctest::Approx(12.0f).epsilon(0.01));
  f.step(600);
  CHECK(f.w().slots[0].energy == doctest::Approx(20.0f));  // capped

  f.db.buildings[kShield].shield.upkeepPerSec = 0.5f;
  const float before = f.w().stardust;
  f.step(60);  // two shields online, 2 s
  CHECK(f.w().stardust == doctest::Approx(before - 2.0f).epsilon(0.01));
}

TEST_CASE("M2.10 a Bomber flies in, circles dropping its bombs, then dives") {
  Fixture f;
  // A lane enemy on lane A (30 degrees).
  REQUIRE(spawnEnemy(f.w(), kBomber, 0) != kInvalidIndex);
  int bombs = 0;
  bool orbited = false;
  for (int i = 0; i < 30 * 60 && f.w().enemies.count > 0; ++i) {
    const uint32_t before = f.w().falling.count;
    f.step();
    if (f.w().enemies.count > 0) {
      const float x = f.w().enemies.posX[0], y = f.w().enemies.posY[0];
      if (bomberState(f.w().enemies.flags[0]) == BomberState::Orbit) {
        orbited = true;
        CHECK(std::sqrt(x * x + y * y) == doctest::Approx(260.0f).epsilon(0.001));
      }
      if (f.w().falling.count > before) {
        ++bombs;
        const uint32_t j = f.w().falling.count - 1;
        CHECK(f.w().falling.type[j] == kBomb);
        CHECK(f.w().falling.dist[j] == doctest::Approx(260.0f).epsilon(0.02));
        CHECK(wrapPi(f.w().falling.bearing[j] - std::atan2(y, x)) == doctest::Approx(0.0f).epsilon(0.02));
      }
    }
  }
  CHECK(orbited);
  CHECK(bombs == 4);
  CHECK(f.count(EventType::PlanetHit) == 1);  // then it dives and lands
  f.fallAll();
  CHECK(f.count(EventType::FallingImpact) == 4);
}

TEST_CASE("M2.10 volleys spawn from wave data, the wave waits for them, the forecast marks them") {
  ContentDB db = shieldContent();
  WaveSetDef ws;
  ws.id = "storm";
  ws.planet = 0;
  ws.sun = 0;
  WaveDef wave;
  wave.wave = 1;
  wave.spawnSeconds = 2.0f;
  wave.spawns.push_back({0, kRed, 1});
  VolleyDef v;
  v.falling = kMeteor;
  v.bearing = 200.0f * kDeg;
  v.spread = 10.0f * kDeg;
  v.count = 6;
  v.startSeconds = 1.0f;
  v.seconds = 3.0f;
  wave.volleys.push_back(v);
  ws.waves.push_back(wave);
  ws.waves.push_back(wave);
  ws.waves[1].wave = 2;
  db.waveSets.push_back(ws);
  SimConfig cfg = sandboxConfig(db);
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();

  const std::vector<WaveForecast> fc = forecast(w, 2);
  REQUIRE(fc.size() == 2);
  REQUIRE(fc[0].entries.size() == 2);
  const ForecastEntry& volley = fc[0].entries[1];
  CHECK(volley.falling == kMeteor);
  CHECK(volley.enemy == kNoIndex);
  CHECK(volley.count == 6);
  CHECK(volley.laneBearing == doctest::Approx(200.0f * kDeg));
  CHECK(volley.spread == doctest::Approx(10.0f * kDeg));
  CHECK(fc[1].entries[1].count == -1);  // icon only beyond exactAhead

  sim.push(cmd::startWave(sim.tick()));
  int spawned = 0, cleared = 0;
  float firstAt = -1.0f;
  for (int i = 0; i < 30 * 30 && cleared == 0; ++i) {
    const uint32_t before = w.falling.count;
    sim.step();
    if (w.falling.count > before) {
      spawned += static_cast<int>(w.falling.count - before);
      if (firstAt < 0.0f) firstAt = w.waveTime;
      const uint32_t j = w.falling.count - 1;
      CHECK(std::fabs(wrapPi(w.falling.bearing[j] - 200.0f * kDeg)) <= 10.0f * kDeg + 1e-4f);
    }
    for (const Event& e : w.events.items) {
      if (e.type == EventType::WaveCleared) {
        cleared = 1;
        CHECK(w.falling.count == 0);  // not before the last meteor came down
      }
    }
    w.events.clear();
  }
  CHECK(spawned == 6);
  CHECK(firstAt == doctest::Approx(1.0f).epsilon(0.05));
  CHECK(cleared == 1);
  CHECK(w.planetHp < w.planetHpMax);  // unshielded: they hit
}

TEST_CASE("M2.10 falling objects, shields and Bombers are deterministic and replay") {
  ContentDB db = shieldContent();
  WaveSetDef ws;
  ws.id = "storm";
  ws.planet = 0;
  ws.sun = 0;
  WaveDef wave;
  wave.wave = 1;
  wave.spawnSeconds = 4.0f;
  wave.spawns.push_back({0, kBomber, 2});
  wave.volleys.push_back(VolleyDef{kMeteor, 150.0f * kDeg, 20.0f * kDeg, 20, 0.5f, 5.0f});
  ws.waves.push_back(wave);
  db.waveSets.push_back(ws);
  db.planets[0].startStardust = 1000.0f;  // so the replay starts with the same purse
  SimConfig cfg = sandboxConfig(db, 42);
  cfg.waveSet = 0;
  auto run = [&](uint32_t seed) {
    SimConfig c = cfg;
    c.seed = seed;
    Sim sim(c);
    sim.push(cmd::placeBuilding(0, 15, kShield, 0.0f));
    sim.push(cmd::startWave(1));
    sim.push(cmd::rotateInput(30, 0.4f));
    sim.push(cmd::rotateInput(90, 0.0f));
    for (int i = 0; i < 30 * 25; ++i) {
      sim.step();
      sim.world().events.clear();
    }
    return std::make_pair(sim.hash(), sim.world().commandLog);
  };
  const auto a = run(42), b = run(42), c = run(43);
  CHECK(a.first == b.first);
  CHECK(a.first != c.first);  // the spread is seeded
  CHECK(replayHash(cfg, a.second, 30 * 25) == a.first);
}

TEST_CASE("M2.10 falling render buffer: one instance per object, shape and seconds to impact") {
  Fixture f;
  spawnFalling(f.w(), kMeteor, 0.0f, 400.0f);
  spawnFalling(f.w(), kBomb, 90.0f * kDeg, 200.0f);
  std::vector<float> buf(2 * kRenderFloatsPerInstance, -1.0f);
  REQUIRE(fillFallingRenderBuffer(f.w(), buf.data(), 2) == 2);
  CHECK(buf[3] == doctest::Approx(400.0f));                        // origin x
  CHECK(buf[12] == doctest::Approx(static_cast<float>(Shape::Star)));
  CHECK(buf[15] == doctest::Approx(300.0f / 60.0f));              // eta of the meteor
  CHECK(buf[16 + 7] == doctest::Approx(200.0f));                   // the bomb's origin y
  CHECK(buf[16 + 15] == doctest::Approx(100.0f / 40.0f));
  CHECK(fillFallingRenderBuffer(f.w(), buf.data(), 1) == 1);       // respects the cap
}
