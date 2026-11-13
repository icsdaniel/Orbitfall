// Content tests: every shipped file loads cleanly, the M1 numbers match the
// brief (§8.2), and each validation rule rejects a bad sample.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "json/mini_json.h"

#ifndef ORBITFALL_CONTENT_DIR
#define ORBITFALL_CONTENT_DIR "../content"
#endif

using namespace orbitfall;

namespace {

using Files = std::map<std::string, std::string>;
constexpr float kDeg = 3.14159265358979323846f / 180.0f;

// The shipped content as path -> text, the base every bad sample is cut from.
Files shipped_files() {
  namespace fs = std::filesystem;
  Files files;
  const fs::path root(ORBITFALL_CONTENT_DIR);
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    files[entry.path().lexically_relative(root).generic_string()] = text.str();
  }
  return files;
}

// Replaces the single occurrence of `from` in `file`; fails the test if the
// shipped text no longer contains it exactly once (so bad samples stay honest).
void edit(Files& files, const std::string& file, const std::string& from, const std::string& to) {
  REQUIRE_MESSAGE(files.count(file) == 1, file);
  std::string& text = files[file];
  const size_t at = text.find(from);
  REQUIRE_MESSAGE(at != std::string::npos, file << " does not contain: " << from);
  REQUIRE_MESSAGE(text.find(from, at + 1) == std::string::npos, file << " contains twice: " << from);
  text.replace(at, from.size(), to);
}

std::vector<std::string> load_errors(const Files& files) {
  ContentDB db;
  std::vector<std::string> errors;
  const bool ok = load_content_from_strings(files, db, errors);
  CHECK(ok == errors.empty());
  return errors;
}

bool any_contains(const std::vector<std::string>& errors, const std::string& a,
                  const std::string& b = std::string()) {
  for (const std::string& e : errors)
    if (e.find(a) != std::string::npos && e.find(b) != std::string::npos) return true;
  return false;
}

std::string joined(const std::vector<std::string>& errors) {
  std::string s;
  for (const std::string& e : errors) s += "\n  " + e;
  return s;
}

// Loads shipped content with one edit and expects an error naming `file` and
// containing `needle`.
void expect_rejected(const std::string& file, const std::string& from, const std::string& to,
                     const std::string& needle) {
  Files files = shipped_files();
  edit(files, file, from, to);
  const std::vector<std::string> errors = load_errors(files);
  CHECK_MESSAGE(any_contains(errors, file, needle),
                "expected an error in " << file << " containing \"" << needle << "\", got:"
                                        << joined(errors));
}

ContentDB shipped_db() {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE_MESSAGE(load_content(ORBITFALL_CONTENT_DIR, db, errors), joined(errors));
  return db;
}

}  // namespace

TEST_SUITE("content") {

TEST_CASE("every shipped content file loads with zero errors") {
  ContentDB db;
  std::vector<std::string> errors;
  const bool ok = load_content(ORBITFALL_CONTENT_DIR, db, errors);
  CHECK_MESSAGE(errors.empty(), joined(errors));
  REQUIRE(ok);
  const Files files = shipped_files();
  // Every file ended up as exactly one definition (rules.json is the +1).
  CHECK(files.size() == db.planets.size() + db.suns.size() + db.buildings.size() +
                            db.enemies.size() + db.waveSets.size() + 1);
  CHECK(db.planets.size() == 1);
  CHECK(db.suns.size() == 1);
  CHECK(db.buildings.size() == 3);
  CHECK(db.enemies.size() == 5);
  CHECK(db.waveSets.size() == 1);
}

TEST_CASE("loading from strings matches loading from the directory") {
  const ContentDB a = shipped_db();
  ContentDB b;
  std::vector<std::string> errors;
  Files prefixed;  // as the GDExtension passes them
  for (const auto& kv : shipped_files()) prefixed["res://content/" + kv.first] = kv.second;
  REQUIRE_MESSAGE(load_content_from_strings(prefixed, b, errors), joined(errors));
  REQUIRE(a.enemies.size() == b.enemies.size());
  for (size_t i = 0; i < a.enemies.size(); ++i) CHECK(a.enemies[i].id == b.enemies[i].id);
  REQUIRE(a.buildings.size() == b.buildings.size());
  for (size_t i = 0; i < a.buildings.size(); ++i) CHECK(a.buildings[i].id == b.buildings[i].id);
  CHECK(b.wave_set_index("m1") == a.wave_set_index("m1"));
}

TEST_CASE("id lookup returns indices, kNoIndex for unknown ids") {
  const ContentDB db = shipped_db();
  for (size_t i = 0; i < db.enemies.size(); ++i)
    CHECK(db.enemy_index(db.enemies[i].id) == static_cast<int>(i));
  for (size_t i = 0; i < db.buildings.size(); ++i)
    CHECK(db.building_index(db.buildings[i].id) == static_cast<int>(i));
  CHECK(db.planet_index("medium") == 0);
  CHECK(db.sun_index("default") == 0);
  CHECK(db.enemy_index("no_such_enemy") == kNoIndex);
  CHECK(db.building_index("") == kNoIndex);
}

TEST_CASE("medium planet matches BRIEF 8.2, degrees converted to radians") {
  const ContentDB db = shipped_db();
  const PlanetDef& p = db.planets[static_cast<size_t>(db.planet_index("medium"))];
  CHECK(p.radius == doctest::Approx(100));
  CHECK(p.slots == 36);
  CHECK(p.hp == doctest::Approx(100));
  CHECK(p.rotMaxSpeed == doctest::Approx(90 * kDeg));
  CHECK(p.rotAccel == doctest::Approx(360 * kDeg));
  CHECK(p.rotDamping == doctest::Approx(4));
  CHECK(p.startStardust == doctest::Approx(150));
  CHECK(p.surfaceTimeScale == doctest::Approx(0.75));
  CHECK(p.sellRefund == doctest::Approx(0.7));
  REQUIRE(p.lanes.size() == 2);
  CHECK(p.lane_index("A") == 0);
  CHECK(p.lane_index("B") == 1);
  CHECK(p.lane_index("C") == kNoIndex);
  CHECK(p.lanes[0].bearing == doctest::Approx(30 * kDeg));
  CHECK(p.lanes[1].bearing == doctest::Approx(170 * kDeg));
}

TEST_CASE("sun and rules match the brief; lit arc is about 90 degrees") {
  const ContentDB db = shipped_db();
  const SunDef& s = db.suns[0];
  CHECK(s.type == SunPathType::ConstantDrift);
  CHECK(s.start == doctest::Approx(0));
  CHECK(s.speed == doctest::Approx(1.5 * kDeg));
  CHECK(s.beamWidth == doctest::Approx(141));
  CHECK(s.baseStrength == doctest::Approx(1));
  const float litArc = 2.0f * std::asin(s.beamWidth / (2.0f * db.planets[0].radius));
  CHECK(litArc / kDeg == doctest::Approx(90).epsilon(0.01));

  CHECK(db.rules.tickRate == 30);
  CHECK(db.rules.killBounty == doctest::Approx(1));
  CHECK(db.rules.minDamageMult == doctest::Approx(0.25));
  CHECK(db.rules.lightChargeMult == doctest::Approx(2));
}

TEST_CASE("buildings match BRIEF 8.2") {
  const ContentDB db = shipped_db();
  const BuildingDef& g = db.buildings[static_cast<size_t>(db.building_index("gatling"))];
  CHECK(g.kind == BuildingKind::Tower);
  CHECK(g.cost == doctest::Approx(50));
  CHECK(g.arc == doctest::Approx(40 * kDeg));
  CHECK(g.range == doctest::Approx(350));
  CHECK(g.fireRate == doctest::Approx(6));
  REQUIRE(g.hasProjectile);
  CHECK(g.projectile.kind == ProjectileKind::Dot);
  CHECK(g.projectile.speed == doctest::Approx(900));
  CHECK(g.projectile.damage == doctest::Approx(1));
  CHECK(g.projectile.splash == doctest::Approx(0));
  CHECK(g.aimMount);
  CHECK(g.aimOffset == doctest::Approx(0));
  REQUIRE(g.targeting.size() == 3);
  CHECK(g.targeting[0] == TargetingMode::First);
  CHECK(g.targeting[1] == TargetingMode::Strong);
  CHECK(g.targeting[2] == TargetingMode::Close);
  CHECK(g.visual.color == "tower.gatling");

  const BuildingDef& c = db.buildings[static_cast<size_t>(db.building_index("cannon"))];
  CHECK(c.cost == doctest::Approx(90));
  CHECK(c.arc == doctest::Approx(25 * kDeg));
  CHECK(c.range == doctest::Approx(450));
  CHECK(c.fireRate == doctest::Approx(0.8));
  CHECK(c.projectile.kind == ProjectileKind::Shell);
  CHECK(c.projectile.speed == doctest::Approx(600));
  CHECK(c.projectile.damage == doctest::Approx(3));
  CHECK(c.projectile.type == DamageType::Explosive);
  CHECK(c.projectile.splash == doctest::Approx(40));
  CHECK(c.visual.shape == Shape::Square);
  REQUIRE(c.falloff.size() == 2);
  CHECK(c.falloff[1].mult == doctest::Approx(1));

  const BuildingDef& s = db.buildings[static_cast<size_t>(db.building_index("solar_panel"))];
  CHECK(s.kind == BuildingKind::Panel);
  CHECK(s.cost == doctest::Approx(40));
  CHECK(s.panelRate == doctest::Approx(3));
  CHECK_FALSE(s.hasProjectile);
  CHECK(s.targeting.empty());
}

TEST_CASE("layer chain drifter_green -> blue -> red resolves to 3 layers") {
  const ContentDB db = shipped_db();
  const int green = db.enemy_index("drifter_green");
  const int blue = db.enemy_index("drifter_blue");
  const int red = db.enemy_index("drifter_red");
  REQUIRE(green != kNoIndex);
  const std::vector<int> chain = db.layer_chain(green);
  REQUIRE(chain.size() == 3);
  CHECK(chain[0] == green);
  CHECK(chain[1] == blue);
  CHECK(chain[2] == red);
  CHECK(db.enemies[static_cast<size_t>(green)].layers == 3);
  CHECK(db.enemies[static_cast<size_t>(green)].popsInto == blue);
  CHECK(db.enemies[static_cast<size_t>(blue)].popsInto == red);
  CHECK(db.enemies[static_cast<size_t>(red)].popsInto == kNoIndex);
  CHECK(db.layer_chain(red).size() == 1);
  CHECK(db.layer_chain(kNoIndex).empty());
  CHECK(db.layer_chain(999).empty());
  // 1 HP per layer, 1 landing damage per remaining layer.
  CHECK(db.enemies[static_cast<size_t>(green)].hp == doctest::Approx(3));
  CHECK(db.enemies[static_cast<size_t>(green)].land_damage(3) == doctest::Approx(3));
  CHECK(db.enemies[static_cast<size_t>(green)].land_damage(1) == doctest::Approx(1));
}

TEST_CASE("dart and eclipser match BRIEF 8.2") {
  const ContentDB db = shipped_db();
  const EnemyDef& d = db.enemies[static_cast<size_t>(db.enemy_index("dart"))];
  CHECK(d.shape == Shape::Triangle);
  CHECK(d.layers == 1);
  CHECK(d.speed == doctest::Approx(110));
  CHECK(d.size == doctest::Approx(6));
  CHECK(d.bounty == doctest::Approx(1));
  CHECK((d.traits & kTraitFast) != 0);
  CHECK(d.visual.trail);
  CHECK_FALSE(d.shadowCaster);
  CHECK(d.behavior == EnemyBehavior::Lane);

  const EnemyDef& e = db.enemies[static_cast<size_t>(db.enemy_index("eclipser"))];
  CHECK(e.behavior == EnemyBehavior::Eclipser);
  CHECK(e.shadowCaster);
  CHECK(e.hpBar);
  CHECK(e.hp == doctest::Approx(40));
  CHECK(e.speed == doctest::Approx(25));
  CHECK(e.size == doctest::Approx(28));
  CHECK(e.bounty == doctest::Approx(15));
  CHECK(e.land_damage(1) == doctest::Approx(15));
  REQUIRE(e.hasHover);
  CHECK(e.hover.hoverRadius == doctest::Approx(260));
  CHECK(e.hover.hoverSeconds == doctest::Approx(15));
  CHECK(e.hover.descendSpeed == doctest::Approx(10));
  CHECK(e.hover.spawnRadius == doctest::Approx(900));
  CHECK(e.hover.spawnJitter == doctest::Approx(6 * kDeg));

  // Drifters cast no shadow; only the Eclipser does in M1.
  int casters = 0;
  for (const EnemyDef& en : db.enemies) casters += en.shadowCaster ? 1 : 0;
  CHECK(casters == 1);
}

TEST_CASE("m1 waves match the BRIEF 8.2 table; wave 5 lightMult is 0.5") {
  const ContentDB db = shipped_db();
  const int set = db.wave_set_index("m1");
  REQUIRE(set != kNoIndex);
  const WaveSetDef& ws = db.waveSets[static_cast<size_t>(set)];
  CHECK(ws.planet == db.planet_index("medium"));
  CHECK(ws.sun == db.sun_index("default"));
  CHECK(ws.autoStartSeconds == doctest::Approx(0));
  REQUIRE(ws.waves.size() == 8);
  CHECK(ws.waves[4].wave == 5);
  CHECK(ws.waves[4].lightMult == doctest::Approx(0.5));

  const float light[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f, 0.7f, 1.0f};
  struct Row {
    int wave;
    const char* lane;
    const char* enemy;
    int count;
  };
  const Row table[] = {
      {1, "A", "drifter_red", 8},    {2, "A", "drifter_blue", 10},  {2, "B", "drifter_red", 4},
      {3, "A", "dart", 6},           {3, "B", "drifter_blue", 10},  {4, "A", "drifter_blue", 12},
      {4, "B", "eclipser", 1},       {5, "A", "drifter_green", 14}, {5, "B", "dart", 8},
      {6, "A", "eclipser", 1},       {6, "A", "dart", 10},          {6, "B", "drifter_blue", 16},
      {7, "A", "drifter_green", 20}, {7, "B", "eclipser", 1},       {7, "B", "drifter_green", 12},
      {8, "A", "eclipser", 2},       {8, "A", "dart", 15},          {8, "B", "drifter_green", 25}};
  const PlanetDef& planet = db.planets[static_cast<size_t>(ws.planet)];
  size_t row = 0;
  for (size_t w = 0; w < ws.waves.size(); ++w) {
    const WaveDef& wave = ws.waves[w];
    CAPTURE(wave.wave);
    CHECK(wave.wave == static_cast<int>(w) + 1);
    CHECK(wave.lightMult == doctest::Approx(light[w]));
    CHECK(wave.spawnSeconds >= 15.0f);
    CHECK(wave.spawnSeconds <= 25.0f);
    CHECK(wave.forecast.exactAhead == 1);
    CHECK(wave.forecast.iconsAhead == 3);
    for (const SpawnDef& s : wave.spawns) {
      REQUIRE(row < sizeof(table) / sizeof(table[0]));
      CHECK(table[row].wave == wave.wave);
      CHECK(s.lane == planet.lane_index(table[row].lane));
      CHECK(s.enemy == db.enemy_index(table[row].enemy));
      CHECK(s.count == table[row].count);
      ++row;
    }
  }
  CHECK(row == sizeof(table) / sizeof(table[0]));
}

// ---- validation: each rule rejects a bad sample -----------------------------

TEST_CASE("rejects malformed JSON with file and position") {
  expect_rejected("enemies/dart.json", "\"speed\": 110,", "\"speed\": 110", "JSON parse error: line 3");
  expect_rejected("rules.json", "{", "[", "JSON parse error");
}

TEST_CASE("rejects unknown references") {
  SUBCASE("popsInto") {
    expect_rejected("enemies/drifter_blue.json", "\"popsInto\": \"drifter_red\"",
                    "\"popsInto\": \"drifter_pink\"", "unknown enemy id 'drifter_pink'");
  }
  SUBCASE("wave enemy") {
    expect_rejected("waves/m1.json", "\"enemy\": \"drifter_red\", \"count\": 8",
                    "\"enemy\": \"blimp\", \"count\": 8", "unknown enemy id 'blimp'");
  }
  SUBCASE("wave lane") {
    expect_rejected("waves/m1.json", "\"lane\": \"B\", \"enemy\": \"drifter_red\"",
                    "\"lane\": \"C\", \"enemy\": \"drifter_red\"", "unknown lane 'C'");
  }
  SUBCASE("wave planet") {
    expect_rejected("waves/m1.json", "\"planet\": \"medium\"", "\"planet\": \"huge\"",
                    "unknown planet id 'huge'");
  }
  SUBCASE("wave sun") {
    expect_rejected("waves/m1.json", "\"sun\": \"default\"", "\"sun\": \"nova\"",
                    "unknown sun id 'nova'");
  }
}

TEST_CASE("rejects unknown enum values") {
  expect_rejected("buildings/cannon.json", "\"kind\": \"tower\"", "\"kind\": \"castle\"",
                  "unknown value 'castle'");
  expect_rejected("buildings/cannon.json", "\"type\": \"explosive\"", "\"type\": \"magic\"",
                  "unknown value 'magic'");
  expect_rejected("buildings/gatling.json", "\"strong\"", "\"weakest\"", "unknown targeting mode");
  expect_rejected("buildings/gatling.json", "\"shape\": \"triangle\"", "\"shape\": \"blob\"",
                  "unknown value 'blob'");
  expect_rejected("enemies/dart.json", "\"traits\": [\"fast\"]", "\"traits\": [\"sneaky\"]",
                  "unknown trait 'sneaky'");
  expect_rejected("enemies/dart.json", "\"behavior\": \"lane\"", "\"behavior\": \"teleport\"",
                  "unknown value 'teleport'");
  expect_rejected("sun/default.json", "constant_drift", "wobble", "unknown value 'wobble'");
}

TEST_CASE("rejects negative numbers") {
  expect_rejected("buildings/gatling.json", "\"cost\": 50", "\"cost\": -50", "must not be negative");
  expect_rejected("buildings/gatling.json", "\"damage\": 1", "\"damage\": -1", "must not be negative");
  expect_rejected("buildings/solar_panel.json", "\"panelRate\": 3", "\"panelRate\": -3",
                  "must not be negative");
  expect_rejected("enemies/dart.json", "\"speed\": 110", "\"speed\": -110", "must not be negative");
  expect_rejected("enemies/dart.json", "\"bounty\": 1", "\"bounty\": -1", "must not be negative");
  expect_rejected("enemies/eclipser.json", "\"hoverSeconds\": 15", "\"hoverSeconds\": -15",
                  "must not be negative");
  expect_rejected("planets/medium.json", "\"hp\": 100", "\"hp\": -100", "must not be negative");
  expect_rejected("planets/medium.json", "\"startStardust\": 150", "\"startStardust\": -1",
                  "must not be negative");
  expect_rejected("sun/default.json", "\"beamWidth\": 141", "\"beamWidth\": -141",
                  "must not be negative");
  expect_rejected("waves/m1.json", "\"count\": 14}", "\"count\": -14}", "must not be negative");
  expect_rejected("waves/m1.json", "\"lightMult\": 0.5", "\"lightMult\": -0.5",
                  "must not be negative");
  expect_rejected("rules.json", "\"killBounty\": 1", "\"killBounty\": -1", "must not be negative");
}

TEST_CASE("rejects out-of-range values") {
  expect_rejected("planets/medium.json", "\"slots\": 36", "\"slots\": 0", "must be >= 1");
  expect_rejected("planets/medium.json", "\"slots\": 36", "\"slots\": 36.5", "whole number");
  expect_rejected("planets/medium.json", "\"sellRefund\": 0.7", "\"sellRefund\": 1.5", "must be <= 1");
  expect_rejected("planets/medium.json", "\"surfaceTimeScale\": 0.75", "\"surfaceTimeScale\": 0",
                  "must be > 0");
  expect_rejected("buildings/cannon.json", "\"aimOffsetDeg\": 0", "\"aimOffsetDeg\": 90",
                  "must be <= 60");
  expect_rejected("buildings/cannon.json", "\"arcDeg\": 25", "\"arcDeg\": 0", "must be > 0");
  expect_rejected("rules.json", "\"tickRate\": 30", "\"tickRate\": 0", "must be >= 1");
  expect_rejected("waves/m1.json", "\"wave\": 3", "\"wave\": 7", "numbered 1, 2, 3");
}

TEST_CASE("rejects damage multipliers below 0.25 (no hard counters)") {
  SUBCASE("enemy resistance") {
    expect_rejected("enemies/dart.json", "\"bounty\": 1,",
                    "\"bounty\": 1, \"damageMult\": { \"kinetic\": 0.1 },", "below the damage multiplier floor");
  }
  SUBCASE("enemy immunity") {
    expect_rejected("enemies/eclipser.json", "\"bounty\": 15,",
                    "\"bounty\": 15, \"damageMult\": { \"explosive\": 0 },", "below the damage multiplier floor");
  }
  SUBCASE("tower falloff") {
    expect_rejected("buildings/cannon.json", "[1, 1]]", "[1, 0.2]]", "below the damage multiplier floor");
  }
  SUBCASE("the floor itself cannot be lowered in rules.json") {
    expect_rejected("rules.json", "\"minDamageMult\": 0.25", "\"minDamageMult\": 0.1", "must be >= 0.25");
  }
  SUBCASE("exactly 0.25 and soft counters are accepted") {
    Files files = shipped_files();
    edit(files, "enemies/dart.json", "\"bounty\": 1,",
         "\"bounty\": 1, \"damageMult\": { \"kinetic\": 0.25, \"cryo\": 2 },");
    ContentDB db;
    std::vector<std::string> errors;
    REQUIRE_MESSAGE(load_content_from_strings(files, db, errors), joined(errors));
    const EnemyDef& d = db.enemies[static_cast<size_t>(db.enemy_index("dart"))];
    CHECK(d.damageMult[static_cast<int>(DamageType::Kinetic)] == doctest::Approx(0.25));
    CHECK(d.damageMult[static_cast<int>(DamageType::Cryo)] == doctest::Approx(2));
    CHECK(d.damageMult[static_cast<int>(DamageType::Energy)] == doctest::Approx(1));
  }
  SUBCASE("a raised floor in rules.json applies to content") {
    Files files = shipped_files();
    edit(files, "rules.json", "\"minDamageMult\": 0.25", "\"minDamageMult\": 0.5");
    edit(files, "enemies/dart.json", "\"bounty\": 1,", "\"bounty\": 1, \"damageMult\": { \"kinetic\": 0.3 },");
    CHECK(any_contains(load_errors(files), "enemies/dart.json", "floor 0.5"));
  }
}

TEST_CASE("rejects missing fields") {
  expect_rejected("buildings/gatling.json", "\"cost\": 50,", "", "missing field 'cost'");
  expect_rejected("buildings/gatling.json", "\"range\": 350,", "", "missing field 'range'");
  expect_rejected("buildings/gatling.json", "\"aimMount\": true,", "", "missing field 'aimMount'");
  expect_rejected("buildings/gatling.json", "\"speed\": 900,", "", "missing field 'speed'");
  expect_rejected("buildings/solar_panel.json", "\"panelRate\": 3,", "", "missing field 'panelRate'");
  expect_rejected("enemies/dart.json", "\"id\": \"dart\",", "", "missing field 'id'");
  expect_rejected("enemies/dart.json", "\"landDamagePerLayer\": 1,", "", "missing field 'landDamagePerLayer'");
  expect_rejected("enemies/dart.json", "\"shadowCaster\": false,", "", "missing field 'shadowCaster'");
  expect_rejected("enemies/eclipser.json", "\"hoverRadius\": 260,", "", "missing field 'hoverRadius'");
  expect_rejected("enemies/drifter_green.json", "\"popsInto\": \"drifter_blue\"", "\"popsInto\": null",
                  "missing field 'popsInto'");
  expect_rejected("planets/medium.json", "\"radius\": 100,", "", "missing field 'radius'");
  expect_rejected("sun/default.json", "\"beamWidth\": 141,", "", "missing field 'beamWidth'");
  expect_rejected("waves/m1.json", "\"lightMult\": 0.5,", "", "missing field 'lightMult'");
  expect_rejected("rules.json", "\"tickRate\": 30,", "", "missing field 'tickRate'");
}

TEST_CASE("rejects wrong types") {
  expect_rejected("buildings/gatling.json", "\"cost\": 50", "\"cost\": \"50\"", "expected number, got string");
  expect_rejected("buildings/gatling.json", "\"aimMount\": true", "\"aimMount\": 1", "expected bool");
  expect_rejected("planets/medium.json", "{ \"id\": \"A\", \"bearingDeg\": 30 }", "\"A\"", "expected object");
}

TEST_CASE("rejects missing files and unknown categories") {
  Files files = shipped_files();
  files.erase("rules.json");
  CHECK(any_contains(load_errors(files), "rules.json", "missing file"));

  files = shipped_files();
  files.erase("planets/medium.json");
  const std::vector<std::string> errors = load_errors(files);
  CHECK(any_contains(errors, "no planet defined"));
  CHECK(any_contains(errors, "waves/m1.json", "unknown planet id 'medium'"));

  files = shipped_files();
  files["gadgets/thing.json"] = "{ \"id\": \"thing\" }";
  CHECK(any_contains(load_errors(files), "unknown content category 'gadgets'"));

  ContentDB db;
  std::vector<std::string> dirErrors;
  CHECK_FALSE(load_content(std::string(ORBITFALL_CONTENT_DIR) + "/no_such_dir", db, dirErrors));
  CHECK(any_contains(dirErrors, "content directory not found"));
}

TEST_CASE("rejects bad ids and broken layer chains") {
  SUBCASE("id must match the file name") {
    expect_rejected("enemies/dart.json", "\"id\": \"dart\"", "\"id\": \"arrow\"", "must match the file name");
  }
  SUBCASE("duplicate id") {
    Files files = shipped_files();
    files["extra/enemies/dart.json"] = files["enemies/dart.json"];
    CHECK(any_contains(load_errors(files), "duplicate id 'dart'"));
  }
  SUBCASE("duplicate lane") {
    expect_rejected("planets/medium.json", "\"id\": \"B\"", "\"id\": \"A\"", "duplicate lane id 'A'");
  }
  SUBCASE("popsInto cycle") {
    expect_rejected("enemies/drifter_red.json", "\"popsInto\": null", "\"popsInto\": \"drifter_green\"",
                    "loops back");
  }
  SUBCASE("layer count must step down by one") {
    expect_rejected("enemies/drifter_green.json", "\"layers\": 3", "\"layers\": 5", "exactly one fewer");
  }
}

TEST_CASE("a failed load leaves the output untouched and reports every problem") {
  Files files = shipped_files();
  edit(files, "buildings/gatling.json", "\"cost\": 50", "\"cost\": -50");
  edit(files, "enemies/dart.json", "\"speed\": 110", "\"speed\": -110");
  ContentDB db = shipped_db();
  const size_t enemiesBefore = db.enemies.size();
  std::vector<std::string> errors;
  CHECK_FALSE(load_content_from_strings(files, db, errors));
  CHECK(errors.size() == 2);
  CHECK(db.enemies.size() == enemiesBefore);
}

TEST_CASE("mini_json parses the JSON grammar and rejects junk") {
  mini_json::Value v;
  std::string err;
  REQUIRE(mini_json::parse(" {\"a\": [1, -2.5e1, true, false, null], \"s\": \"x\\n\\u00e9\\ud83d\\ude00\"} ", v, err));
  REQUIRE(v.is_object());
  const mini_json::Value* a = v.find("a");
  REQUIRE(a != nullptr);
  REQUIRE(a->array.size() == 5);
  CHECK(a->array[0].number == 1.0);
  CHECK(a->array[1].number == -25.0);
  CHECK(a->array[2].boolean);
  CHECK(a->array[4].is_null());
  CHECK(v.find("s")->string == "x\n\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(v.find("missing") == nullptr);

  const char* bad[] = {"", "{", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "01", "1.", "tru", "\"abc",
                       "{\"a\":1} x", "{\"a\":1,\"a\":2}", "'a'", "[1 2]", "\"\\q\"", "nul"};
  for (const char* text : bad) {
    CAPTURE(text);
    err.clear();
    CHECK_FALSE(mini_json::parse(text, v, err));
    CHECK_FALSE(err.empty());
  }
  std::string deep(200, '[');
  CHECK_FALSE(mini_json::parse(deep, v, err));
}

}  // TEST_SUITE
