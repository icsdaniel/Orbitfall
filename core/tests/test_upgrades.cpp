// M2.2 acceptance (sim part): stat modifiers, path exclusivity, the
// UpgradeBuilding command and its rejections, upgraded stats in combat and
// income, sell refunds and replay determinism.
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "systems/upgrades.h"
#include "systems/waves.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

UpgradeTierDef tier(float cost, std::vector<StatModifier> mods) {
  UpgradeTierDef t;
  t.name = "t";
  t.cost = cost;
  t.mods = std::move(mods);
  return t;
}

StatModifier mul(UpgradeStat s, float v) { return {s, ModOp::Mul, v}; }
StatModifier add(UpgradeStat s, float v) { return {s, ModOp::Add, v}; }

// tinyContent() plus 2 x 4 trees on the gatling (cost 50) and the panel (cost 40).
// Gatling path 0 "rate": fireRate x2, x1.5, +3, x2 (costs 10, 20, 30, 40).
// Gatling path 1 "reach": range x2, arc +10 deg, damage +1 and splash +20, damage x3.
// Panel path 0: income x2 per tier; path 1: income +1 per tier (cost 5 each).
ContentDB upgradeContent() {
  ContentDB db = tinyContent();
  db.planets[0].startStardust = 1000.0f;
  BuildingDef& g = db.buildings[kGatling];
  UpgradePathDef rate;
  rate.id = "rate";
  rate.tiers = {tier(10, {mul(UpgradeStat::FireRate, 2.0f)}), tier(20, {mul(UpgradeStat::FireRate, 1.5f)}),
                tier(30, {add(UpgradeStat::FireRate, 3.0f)}), tier(40, {mul(UpgradeStat::FireRate, 2.0f)})};
  UpgradePathDef reach;
  reach.id = "reach";
  reach.tiers = {tier(10, {mul(UpgradeStat::Range, 2.0f)}), tier(20, {add(UpgradeStat::Arc, 10.0f * kDeg)}),
                 tier(30, {add(UpgradeStat::Damage, 1.0f), add(UpgradeStat::Splash, 20.0f)}),
                 tier(40, {mul(UpgradeStat::Damage, 3.0f)})};
  g.paths = {rate, reach};
  BuildingDef& p = db.buildings[kPanel];
  UpgradePathDef eff, arr;
  eff.id = "eff";
  arr.id = "arr";
  for (int i = 0; i < 4; ++i) {
    eff.tiers.push_back(tier(5, {mul(UpgradeStat::Income, 2.0f)}));
    arr.tiers.push_back(tier(5, {add(UpgradeStat::Income, 1.0f)}));
  }
  p.paths = {eff, arr};
  return db;
}

std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

// Applies `n` upgrades of `path` on `slot`, one tick each.
void upgrade(Sim& sim, int32_t slot, int32_t path, int n = 1) {
  for (int i = 0; i < n; ++i) {
    sim.push(cmd::upgradeBuilding(sim.tick(), slot, path));
    sim.step();
  }
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

}  // namespace

TEST_CASE("upgrades: no tiers means the definition's own stats") {
  const ContentDB db = upgradeContent();
  const uint8_t none[kUpgradePaths] = {0, 0};
  const BuildingStats s = effectiveStats(db.buildings[kGatling], none);
  CHECK(s.damage == 1.0f);
  CHECK(s.fireRate == 6.0f);
  CHECK(s.range == 350.0f);
  CHECK(s.arc == doctest::Approx(40.0f * kDeg));
  CHECK(s.splash == 0.0f);
  CHECK(effectiveStats(db.buildings[kPanel], none).income == 3.0f);
  // A building without paths ignores stray tiers.
  const uint8_t stray[kUpgradePaths] = {3, 1};
  CHECK(effectiveStats(db.buildings[kCannon], stray).range == 450.0f);
}

TEST_CASE("upgrades: modifiers stack as (base + adds) * muls, whatever the order bought") {
  const ContentDB db = upgradeContent();
  const BuildingDef& g = db.buildings[kGatling];
  const uint8_t r1[kUpgradePaths] = {1, 0};
  CHECK(effectiveStats(g, r1).fireRate == doctest::Approx(12.0f));
  const uint8_t r2[kUpgradePaths] = {2, 0};
  CHECK(effectiveStats(g, r2).fireRate == doctest::Approx(18.0f));
  // Tier 3 adds 3 to the base before the multipliers: (6 + 3) * 2 * 1.5.
  const uint8_t r3[kUpgradePaths] = {3, 0};
  CHECK(effectiveStats(g, r3).fireRate == doctest::Approx(27.0f));
  const uint8_t r4[kUpgradePaths] = {4, 0};
  CHECK(effectiveStats(g, r4).fireRate == doctest::Approx(54.0f));

  // Both paths combine; untouched stats stay.
  const uint8_t both[kUpgradePaths] = {2, 4};
  const BuildingStats s = effectiveStats(g, both);
  CHECK(s.fireRate == doctest::Approx(18.0f));
  CHECK(s.range == doctest::Approx(700.0f));
  CHECK(s.arc == doctest::Approx(50.0f * kDeg));
  CHECK(s.damage == doctest::Approx(6.0f));  // (1 + 1) * 3
  CHECK(s.splash == doctest::Approx(20.0f));
  CHECK(s.get(UpgradeStat::Range) == s.range);
  CHECK(s.get(UpgradeStat::Damage) == s.damage);

  // Panel: add path and mul path, (3 + 2) * 4.
  const uint8_t panel[kUpgradePaths] = {2, 2};
  CHECK(effectiveStats(db.buildings[kPanel], panel).income == doctest::Approx(20.0f));
}

TEST_CASE("upgrades: the arc never exceeds a half turn and stats never go negative") {
  ContentDB db = upgradeContent();
  BuildingDef& g = db.buildings[kGatling];
  g.paths[1].tiers[1].mods = {add(UpgradeStat::Arc, 400.0f * kDeg), add(UpgradeStat::Damage, -5.0f)};
  const uint8_t t[kUpgradePaths] = {0, 2};
  CHECK(effectiveStats(g, t).arc == doctest::Approx(kPi));
  CHECK(effectiveStats(g, t).damage == 0.0f);
}

TEST_CASE("upgrades: paths are exclusive past the rules tier") {
  const ContentDB db = upgradeContent();
  const BuildingDef& g = db.buildings[kGatling];
  REQUIRE(db.rules.upgradeExclusiveTier == 2);
  const uint8_t fresh[kUpgradePaths] = {0, 0};
  CHECK(upgradeTierCap(db.rules, g, fresh, 0) == 4);
  CHECK(upgradeTierCap(db.rules, g, fresh, 1) == 4);
  const uint8_t even[kUpgradePaths] = {2, 2};
  CHECK(upgradeTierCap(db.rules, g, even, 0) == 4);
  CHECK(upgradeTierCap(db.rules, g, even, 1) == 4);
  const uint8_t committed[kUpgradePaths] = {3, 1};
  CHECK(upgradeTierCap(db.rules, g, committed, 0) == 4);
  CHECK(upgradeTierCap(db.rules, g, committed, 1) == 2);
  // No such path, or a building without paths.
  CHECK(upgradeTierCap(db.rules, g, fresh, 2) == 0);
  CHECK(upgradeTierCap(db.rules, g, fresh, -1) == 0);
  CHECK(upgradeTierCap(db.rules, db.buildings[kCannon], fresh, 0) == 0);

  // The tier is data: with 1, the second path stops at 1; with 4, nothing is exclusive.
  RulesDef rules = db.rules;
  rules.upgradeExclusiveTier = 1;
  const uint8_t two[kUpgradePaths] = {2, 0};
  CHECK(upgradeTierCap(rules, g, two, 1) == 1);
  rules.upgradeExclusiveTier = 4;
  const uint8_t top[kUpgradePaths] = {4, 0};
  CHECK(upgradeTierCap(rules, g, top, 1) == 4);
}

TEST_CASE("upgrades: the preview gives cost and stats before and after the next tier") {
  const ContentDB db = upgradeContent();
  const BuildingDef& g = db.buildings[kGatling];
  const uint8_t t[kUpgradePaths] = {1, 0};
  UpgradePreview pv = upgradePreview(db.rules, g, t, 0, 100.0f);
  CHECK(pv.reason == RejectReason::None);
  CHECK(pv.hasNext);
  CHECK(pv.tier == 1);
  CHECK(pv.maxTier == 4);
  CHECK(pv.cap == 4);
  CHECK(pv.cost == 20.0f);
  CHECK(pv.before.fireRate == doctest::Approx(12.0f));
  CHECK(pv.after.fireRate == doctest::Approx(18.0f));
  CHECK(pv.after.range == pv.before.range);

  // Unaffordable still previews.
  pv = upgradePreview(db.rules, g, t, 0, 19.0f);
  CHECK(pv.reason == RejectReason::Unaffordable);
  CHECK(pv.after.fireRate == doctest::Approx(18.0f));

  // Capped by the other path: blocked, but the next tier is still described.
  const uint8_t committed[kUpgradePaths] = {3, 2};
  pv = upgradePreview(db.rules, g, committed, 1, 1000.0f);
  CHECK(pv.reason == RejectReason::UpgradeBlocked);
  CHECK(pv.hasNext);
  CHECK(pv.cap == 2);
  CHECK(pv.cost == 30.0f);

  // Top tier: nothing next.
  const uint8_t top[kUpgradePaths] = {4, 2};
  pv = upgradePreview(db.rules, g, top, 0, 1000.0f);
  CHECK(pv.reason == RejectReason::UpgradeBlocked);
  CHECK_FALSE(pv.hasNext);
  CHECK(pv.after.fireRate == pv.before.fireRate);
}

TEST_CASE("upgrades: the command pays the tier cost in stardust and raises the tier") {
  const ContentDB db = upgradeContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 3, kGatling, 0.0f));
  sim.step();
  sim.world().events.clear();
  upgrade(sim, 3, 0);
  const World& w = sim.world();
  CHECK(w.slots[3].tiers[0] == 1);
  CHECK(w.slots[3].tiers[1] == 0);
  CHECK(w.stardust == 1000.0f - 50.0f - 10.0f);
  const auto up = eventsOf(sim, EventType::BuildingUpgraded);
  REQUIRE(up.size() == 1);
  CHECK(up[0].a == 3);
  CHECK(up[0].b == 0);
  CHECK(up[0].x == 1.0f);
  CHECK(up[0].value == 10.0f);
  const auto dust = eventsOf(sim, EventType::StardustChanged);
  REQUIRE(dust.size() == 1);
  CHECK(dust[0].value == 940.0f);

  upgrade(sim, 3, 1, 2);
  CHECK(w.slots[3].tiers[1] == 2);
  CHECK(w.stardust == 940.0f - 30.0f);
  CHECK(upgradeSpent(db.buildings[kGatling], w.slots[3].tiers) == 40.0f);
}

TEST_CASE("upgrades: one path reaches the top, the other is then blocked at the exclusive tier") {
  const ContentDB db = upgradeContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kGatling, 0.0f));
  sim.step();
  upgrade(sim, 0, 1, 2);
  upgrade(sim, 0, 0, 4);
  World& w = sim.world();
  CHECK(w.slots[0].tiers[0] == 4);
  CHECK(w.slots[0].tiers[1] == 2);
  const float before = w.stardust;
  w.events.clear();

  upgrade(sim, 0, 1);  // capped by path 0
  upgrade(sim, 0, 0);  // already at the top
  upgrade(sim, 0, 2);  // no such path
  upgrade(sim, 0, -1);
  CHECK(w.slots[0].tiers[0] == 4);
  CHECK(w.slots[0].tiers[1] == 2);
  CHECK(w.stardust == before);
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 4);
  for (const Event& e : rejected) {
    CHECK(e.a == 0);
    CHECK(e.b == static_cast<int32_t>(RejectReason::UpgradeBlocked));
    CHECK(e.x == static_cast<float>(static_cast<int>(CommandType::UpgradeBuilding)));
  }
  CHECK(eventsOf(sim, EventType::BuildingUpgraded).empty());
}

TEST_CASE("upgrades: unaffordable, empty and invalid slots, buildings without paths and ended runs are rejected") {
  ContentDB db = upgradeContent();
  db.planets[0].startStardust = 55.0f;
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 2, kGatling, 0.0f));  // 5 left
  sim.step();
  World& w = sim.world();
  w.events.clear();
  upgrade(sim, 2, 0);   // costs 10
  upgrade(sim, 5, 0);   // empty
  upgrade(sim, 99, 0);  // invalid
  upgrade(sim, -1, 0);
  CHECK(w.slots[2].tiers[0] == 0);
  CHECK(w.stardust == 5.0f);
  auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 4);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::Unaffordable));
  CHECK(rejected[0].value == 10.0f);
  CHECK(rejected[1].b == static_cast<int32_t>(RejectReason::EmptySlot));
  CHECK(rejected[2].b == static_cast<int32_t>(RejectReason::InvalidSlot));
  CHECK(rejected[3].b == static_cast<int32_t>(RejectReason::InvalidSlot));

  // A building without paths.
  w.stardust = 500.0f;
  sim.push(cmd::placeBuilding(sim.tick(), 7, kCannon, 0.0f));
  sim.step();
  w.events.clear();
  upgrade(sim, 7, 0);
  rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::UpgradeBlocked));

  // After the run ended.
  w.phase = Phase::Lost;
  w.events.clear();
  upgrade(sim, 2, 0);
  rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::WrongPhase));
  CHECK(w.slots[2].tiers[0] == 0);
}

TEST_CASE("upgrades: selling refunds (cost + upgrades paid) x sellRefund and clears the tiers") {
  const ContentDB db = upgradeContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 4, kGatling, 0.0f));
  sim.step();
  upgrade(sim, 4, 0, 3);  // 10 + 20 + 30
  upgrade(sim, 4, 1, 1);  // 10
  World& w = sim.world();
  const float before = w.stardust;
  CHECK(before == 1000.0f - 50.0f - 70.0f);
  w.events.clear();
  sim.push(cmd::sellBuilding(sim.tick(), 4));
  sim.step();
  CHECK(w.stardust == doctest::Approx(before + (50.0f + 70.0f) * 0.7f));
  const auto sold = eventsOf(sim, EventType::BuildingSold);
  REQUIRE(sold.size() == 1);
  CHECK(sold[0].value == doctest::Approx(84.0f));
  CHECK(w.slots[4].type == -1);
  CHECK(w.slots[4].tiers[0] == 0);
  CHECK(w.slots[4].tiers[1] == 0);

  // A new building on the slot starts without upgrades.
  sim.push(cmd::placeBuilding(sim.tick(), 4, kGatling, 0.0f));
  sim.step();
  CHECK(w.slots[4].tiers[0] == 0);
  CHECK(effectiveStats(db.buildings[kGatling], w.slots[4].tiers).fireRate == 6.0f);
}

TEST_CASE("upgrades: a fire-rate tier doubles the shots, a range tier reaches further") {
  const ContentDB db = upgradeContent();
  // Shots fired in 60 ticks at a target that never dies.
  const auto shots = [&](int rateTiers) {
    Sim sim(sandboxConfig(db));
    sim.push(cmd::placeBuilding(0, 0, kGatling, 0.0f));
    sim.step();
    upgrade(sim, 0, 0, rateTiers);
    World& w = sim.world();
    uint32_t fired = 0;
    for (int i = 0; i < 60; ++i) {
      // A fresh target each tick, far enough that no shot lands in between.
      w.enemies.clear();
      const uint32_t e = spawnEnemy(w, kRed, 0);
      w.enemies.posX[e] = 400.0f;
      w.enemies.posY[e] = 0.0f;
      w.enemies.velX[e] = 0.0f;
      w.enemies.velY[e] = 0.0f;
      const uint32_t before = w.projectiles.count;
      w.projectiles.clear();
      sim.step();
      fired += w.projectiles.count;
      (void)before;
    }
    return fired;
  };
  const uint32_t base = shots(0);
  const uint32_t doubled = shots(1);
  CHECK(base == doctest::Approx(12).epsilon(0.1));  // 6 shots/s for 2 s
  CHECK(doubled == doctest::Approx(24).epsilon(0.1));

  // Range x2 (350 -> 700): an enemy 500 from the tower is only reachable upgraded.
  for (int reach = 0; reach <= 1; ++reach) {
    Sim sim(sandboxConfig(db));
    sim.push(cmd::placeBuilding(0, 0, kGatling, 0.0f));
    sim.step();
    upgrade(sim, 0, 1, reach);
    World& w = sim.world();
    const uint32_t e = spawnEnemy(w, kRed, 0);
    w.enemies.posX[e] = 600.0f;
    w.enemies.posY[e] = 0.0f;
    w.enemies.velX[e] = 0.0f;
    w.enemies.velY[e] = 0.0f;
    sim.step();
    sim.step();
    CHECK(((w.slots[0].flags & kSlotHasTarget) != 0) == (reach == 1));
    CHECK((w.projectiles.count > 0) == (reach == 1));
    if (reach == 1) {
      CHECK(w.projectiles.range[0] == doctest::Approx(700.0f));
      CHECK(w.projectiles.life[0] > 350.0f / 900.0f);  // flies the upgraded range
    }
  }
}

TEST_CASE("upgrades: damage and splash tiers ride on the projectile and hit the neighbours") {
  const ContentDB db = upgradeContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kGatling, 0.0f));
  sim.step();
  upgrade(sim, 0, 1, 3);  // damage 1 -> 2, splash 0 -> 20
  World& w = sim.world();
  // A green (3 layers of 1 hp) in the line of fire and a red 15 units beside it.
  const uint32_t a = spawnEnemy(w, kGreen, 0);
  w.enemies.posX[a] = 300.0f;
  w.enemies.posY[a] = 0.0f;
  w.enemies.velX[a] = w.enemies.velY[a] = 0.0f;
  const uint32_t b = spawnEnemy(w, kRed, 0);
  w.enemies.posX[b] = 300.0f;
  w.enemies.posY[b] = 15.0f;
  w.enemies.velX[b] = w.enemies.velY[b] = 0.0f;
  bool sawShot = false;
  uint32_t kills = 0, pops = 0;
  for (int i = 0; i < 12 && kills == 0; ++i) {
    sim.step();
    if (w.projectiles.count > 0) {
      sawShot = true;
      CHECK(w.projectiles.damage[0] == doctest::Approx(2.0f));
      CHECK(w.projectiles.splash[0] == doctest::Approx(20.0f));
    }
    kills += static_cast<uint32_t>(eventsOf(sim, EventType::EnemyKilled).size());
    pops += static_cast<uint32_t>(eventsOf(sim, EventType::LayerPopped).size());
    w.events.clear();
  }
  CHECK(sawShot);
  CHECK(kills == 1);  // the red beside the target died from the blast
  CHECK(pops == 2);   // one shot of damage 2 took two layers off the green
}

TEST_CASE("upgrades: an upgraded lit panel earns its upgraded rate") {
  const ContentDB db = upgradeContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kPanel, 0.0f));  // slot 0 faces the sun (bearing 0)
  sim.step();
  sim.step();
  World& w = sim.world();
  CHECK(w.incomePerSecond == doctest::Approx(3.0f));
  upgrade(sim, 0, 0, 2);  // x4
  upgrade(sim, 0, 1, 1);  // +1
  sim.step();
  CHECK(w.incomePerSecond == doctest::Approx(16.0f));
  const float before = w.stardust;
  for (int i = 0; i < 30; ++i) sim.step();
  CHECK(w.stardust - before == doctest::Approx(16.0f).epsilon(0.01));
}

TEST_CASE("upgrades: tiers are part of the state hash; seed + log replays an upgraded run") {
  const ContentDB& db = shipped();
  const int gatling = db.building_index("gatling");
  const int cannon = db.building_index("cannon");
  const int panel = db.building_index("solar_panel");
  REQUIRE(gatling >= 0);
  REQUIRE(cannon >= 0);
  REQUIRE(panel >= 0);

  const auto run = [&](bool upgrades, std::vector<Command>& log, uint32_t& ticks, uint8_t tiersOut[2]) {
    SimConfig cfg;
    cfg.seed = 77;
    cfg.content = &db;
    cfg.waveSet = 0;
    Sim sim(cfg);
    World& w = sim.world();
    w.recordCommands = true;
    w.stardust = 2000.0f;
    sim.push(cmd::placeBuilding(0, 3, gatling, 0.0f));
    sim.push(cmd::placeBuilding(0, 4, cannon, 0.0f));
    sim.push(cmd::placeBuilding(0, 0, panel, 0.0f));
    sim.push(cmd::startWave(2));
    if (upgrades) {
      sim.push(cmd::upgradeBuilding(1, 3, 0));
      sim.push(cmd::upgradeBuilding(1, 0, 1));
      sim.push(cmd::upgradeBuilding(40, 3, 0));
      sim.push(cmd::upgradeBuilding(90, 4, 1));
      sim.push(cmd::upgradeBuilding(150, 3, 0));
      sim.push(cmd::upgradeBuilding(151, 3, 1));
      sim.push(cmd::upgradeBuilding(152, 3, 1));
      sim.push(cmd::upgradeBuilding(153, 3, 1));  // rejected: path 0 is past the exclusive tier
      sim.push(cmd::upgradeBuilding(300, 9, 0));  // rejected: empty slot
      sim.push(cmd::sellBuilding(500, 4));
    }
    for (int i = 0; i < 900; ++i) sim.step();
    log = w.commandLog;
    ticks = sim.tick();
    tiersOut[0] = w.slots[3].tiers[0];
    tiersOut[1] = w.slots[3].tiers[1];
    return sim.hash();
  };

  std::vector<Command> log, log2, plainLog;
  uint32_t ticks = 0, ticks2 = 0, plainTicks = 0;
  uint8_t tiers[2] = {0, 0}, tiers2[2] = {0, 0}, plainTiers[2] = {0, 0};
  const uint64_t live = run(true, log, ticks, tiers);
  CHECK(tiers[0] == 3);
  CHECK(tiers[1] == 2);
  CHECK(run(true, log2, ticks2, tiers2) == live);
  CHECK(run(false, plainLog, plainTicks, plainTiers) != live);

  // Seed + log alone give the same hash. The cheat (w.stardust) is not in the
  // log, so replay through a Sim that gets the same start.
  SimConfig cfg;
  cfg.seed = 77;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim replay(cfg);
  replay.world().stardust = 2000.0f;
  for (const Command& c : log) replay.push(c);
  for (uint32_t i = 0; i < ticks; ++i) replay.step();
  CHECK(replay.hash() == live);

  // And without any cheat, through replayHash: upgrades bought from the start stardust.
  Sim honest(cfg);
  honest.world().recordCommands = true;
  honest.push(cmd::placeBuilding(0, 3, gatling, 0.0f));
  honest.push(cmd::upgradeBuilding(1, 3, 0));
  honest.push(cmd::upgradeBuilding(2, 3, 1));
  honest.push(cmd::startWave(3));
  for (int i = 0; i < 600; ++i) honest.step();
  CHECK(honest.world().slots[3].tiers[0] == 1);
  CHECK(honest.world().slots[3].tiers[1] == 1);
  CHECK(replayHash(cfg, honest.world().commandLog, honest.tick()) == honest.hash());

  // Two worlds that differ only in a tier hash differently.
  Sim a(cfg), b(cfg);
  a.step();
  b.step();
  CHECK(a.hash() == b.hash());
  b.world().slots[5].tiers[1] = 1;
  CHECK(a.hash() != b.hash());
}

TEST_CASE("upgrades: work on elevated sites and wide footprints, and stack with the M2.1 bonuses") {
  ContentDB db = upgradeContent();
  db.buildings[kGatling].layer = BuildingLayer::Any;
  BuildingDef duo = db.buildings[kGatling];  // 2-slot gatling with the same tree
  duo.id = "test_duo_up";
  duo.footprint = 2;
  db.buildings.push_back(duo);
  const int kDuo = static_cast<int>(db.buildings.size()) - 1;
  db.buildingIds[duo.id] = kDuo;
  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const int32_t n = static_cast<int32_t>(w.slotCount());
  REQUIRE(!w.elevated.empty());
  // Ground foundation on slot 3, a gatling above it (site n + 3).
  sim.push(cmd::placeBuilding(sim.tick(), 3, kPanel, 0.0f));
  sim.step();
  sim.push(cmd::placeBuilding(sim.tick(), n + 3, kGatling, 0.0f));
  sim.step();
  REQUIRE(w.site(static_cast<uint32_t>(n + 3)).type == kGatling);
  upgrade(sim, n + 3, 1);  // range x2
  const BuildingSlot& up = w.site(static_cast<uint32_t>(n + 3));
  CHECK(up.tiers[1] == 1);
  const BuildingStats s = slotStats(db.buildings[kGatling], up);
  CHECK(s.range == doctest::Approx(db.buildings[kGatling].range * 2.0f * (1.0f + db.rules.elevatedRangeBonus)));

  // A 2-slot building: upgrading through its second slot buys on the anchor.
  sim.push(cmd::placeBuilding(sim.tick(), 10, kDuo, 0.0f));
  sim.step();
  w.events.clear();
  upgrade(sim, 11, 0);
  CHECK(w.slots[10].tiers[0] == 1);
  CHECK(w.slots[11].tiers[0] == 0);
  const auto ev = eventsOf(sim, EventType::BuildingUpgraded);
  REQUIRE(ev.size() == 1);
  CHECK(ev[0].a == 10);

  // Selling the elevated building refunds its upgrade too.
  w.events.clear();
  sim.push(cmd::sellBuilding(sim.tick(), n + 3));
  sim.step();
  const auto sold = eventsOf(sim, EventType::BuildingSold);
  REQUIRE(sold.size() == 1);
  CHECK(sold[0].value == doctest::Approx((db.buildings[kGatling].cost + 10.0f) * w.planet->sellRefund));
}
