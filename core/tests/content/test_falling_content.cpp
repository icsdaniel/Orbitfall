// M2.10 content tests: content/falling/*.json (meteor, bomb), the Shield
// building, the Bomber enemy and the m2_storm demo wave set load with the
// numbers below, every falling object is telegraphed for the full warning
// time, and the loader rejects broken samples.
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"

#ifndef ORBITFALL_CONTENT_DIR
#define ORBITFALL_CONTENT_DIR "../content"
#endif

using namespace orbitfall;

namespace {

using Files = std::map<std::string, std::string>;
constexpr float kDeg = 3.14159265358979323846f / 180.0f;

Files shippedFiles() {
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

std::string joined(const std::vector<std::string>& errors) {
  std::string s;
  for (const std::string& e : errors) s += "\n  " + e;
  return s;
}

ContentDB shipped() {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE_MESSAGE(load_content(ORBITFALL_CONTENT_DIR, db, errors), joined(errors));
  return db;
}

// Shipped content with one exact edit; returns the loader's errors.
std::vector<std::string> errorsWith(const std::string& file, const std::string& from, const std::string& to) {
  Files files = shippedFiles();
  REQUIRE(files.count(file) == 1);
  std::string& text = files[file];
  const size_t at = text.find(from);
  REQUIRE_MESSAGE(at != std::string::npos, file << " does not contain: " << from);
  text.replace(at, from.size(), to);
  ContentDB db;
  std::vector<std::string> errors;
  load_content_from_strings(files, db, errors);
  return errors;
}

bool anyContains(const std::vector<std::string>& errors, const std::string& a, const std::string& b) {
  for (const std::string& e : errors)
    if (e.find(a) != std::string::npos && e.find(b) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_SUITE("content") {

TEST_CASE("M2.10 falling objects: meteor and bomb") {
  const ContentDB db = shipped();
  REQUIRE(db.falling_index("meteor") != kNoIndex);
  REQUIRE(db.falling_index("bomb") != kNoIndex);
  const FallingDef& m = db.falling[static_cast<size_t>(db.falling_index("meteor"))];
  CHECK(m.damage == doctest::Approx(4));
  CHECK(m.speed == doctest::Approx(70));
  CHECK(m.size == doctest::Approx(6));
  CHECK(m.spawnRadius == doctest::Approx(650));
  CHECK(m.visual.shape == Shape::Star);
  CHECK(m.visual.color == "falling.meteor");
  const FallingDef& b = db.falling[static_cast<size_t>(db.falling_index("bomb"))];
  CHECK(b.damage == doctest::Approx(5));
  CHECK(b.speed == doctest::Approx(45));
  CHECK(b.spawnRadius == doctest::Approx(0));  // only ever dropped
  CHECK(db.rules.fallingWarnSeconds == doctest::Approx(3));  // default (PLAN M2.10: 3 s warning)
}

TEST_CASE("M2.10 shield building") {
  const ContentDB db = shipped();
  const int i = db.building_index("shield");
  REQUIRE(i != kNoIndex);
  const BuildingDef& s = db.buildings[static_cast<size_t>(i)];
  CHECK(s.kind == BuildingKind::Shield);
  CHECK(s.cost == doctest::Approx(90));
  CHECK(s.shield.width == doctest::Approx(60 * kDeg));
  CHECK(s.shield.height == doctest::Approx(40));
  CHECK(s.shield.energy == doctest::Approx(28));
  CHECK(s.shield.rechargeSeconds == doctest::Approx(12));
  CHECK(s.shield.regenPerSec == doctest::Approx(1));
  CHECK(s.shield.upkeepPerSec == doctest::Approx(0));  // DESIGN Q23 default: off in M2
  CHECK(s.visual.shape == Shape::Hexagon);
  CHECK(db.rules.lightChargeMult == doctest::Approx(2));  // recharges 2x faster while lit
}

TEST_CASE("M2.10 bomber drops bombs that are telegraphed for the full warning time") {
  const ContentDB db = shipped();
  const int i = db.enemy_index("bomber");
  REQUIRE(i != kNoIndex);
  const EnemyDef& e = db.enemies[static_cast<size_t>(i)];
  CHECK(e.behavior == EnemyBehavior::Bomber);
  REQUIRE(e.hasBomber);
  CHECK(e.bomber.bomb == db.falling_index("bomb"));
  CHECK(e.bomber.bombs == 4);
  CHECK(e.bomber.orbitRadius == doctest::Approx(260));
  CHECK(e.bomber.orbitSpeed == doctest::Approx(12 * kDeg));
  CHECK(e.bomber.dropSeconds == doctest::Approx(2.5));
  CHECK_FALSE(e.shadowCaster);
  CHECK(e.shape == Shape::Hexagon);
  // A bomb dropped from the orbit is seen for at least the warning time.
  for (const PlanetDef& p : db.planets) {
    const FallingDef& bomb = db.falling[static_cast<size_t>(e.bomber.bomb)];
    CHECK((e.bomber.orbitRadius - p.radius) / bomb.speed >= db.rules.fallingWarnSeconds);
  }
}

TEST_CASE("M2.10 demo wave set m2_storm: volleys and a meteor storm, M1 waves untouched") {
  const ContentDB db = shipped();
  const int set = db.wave_set_index("m2_storm");
  REQUIRE(set != kNoIndex);
  const WaveSetDef& ws = db.waveSets[static_cast<size_t>(set)];
  REQUIRE(ws.waves.size() == 5);
  const PlanetDef& planet = db.planets[static_cast<size_t>(ws.planet)];
  int biggest = 0;
  for (const WaveDef& w : ws.waves) {
    REQUIRE_FALSE(w.volleys.empty());
    for (const VolleyDef& v : w.volleys) {
      REQUIRE(v.falling != kNoIndex);
      const FallingDef& f = db.falling[static_cast<size_t>(v.falling)];
      // Telegraphed: every volley object falls for longer than the warning.
      CHECK((f.spawnRadius - planet.radius) / f.speed >= db.rules.fallingWarnSeconds);
      if (v.count > biggest) biggest = v.count;
    }
  }
  CHECK(biggest >= 40);  // the storm
  // The M1 waves have no falling objects.
  for (const WaveDef& w : db.waveSets[static_cast<size_t>(db.wave_set_index("m1"))].waves)
    CHECK(w.volleys.empty());
  const VolleyDef& storm = ws.waves[4].volleys[0];
  CHECK(storm.bearing == doctest::Approx(300 * kDeg));
  CHECK(storm.spread == doctest::Approx(20 * kDeg));
  CHECK(storm.count == 45);
}

TEST_CASE("M2.10 loader rejects broken falling, shield, bomber and volley samples") {
  CHECK(anyContains(errorsWith("falling/meteor.json", "\"speed\": 70", "\"speed\": 0"), "falling/meteor.json",
                    "'speed'"));
  CHECK(anyContains(errorsWith("falling/meteor.json", "\"id\": \"meteor\"", "\"id\": \"rock\""),
                    "falling/meteor.json", "must match the file name"));
  CHECK(anyContains(errorsWith("buildings/shield.json", "\"energy\": 28, ", ""), "buildings/shield.json",
                    "missing field 'energy'"));
  CHECK(anyContains(errorsWith("buildings/shield.json", "\"widthDeg\": 60", "\"widthDeg\": 0"),
                    "buildings/shield.json", "'widthDeg'"));
  CHECK(anyContains(errorsWith("enemies/bomber.json", "\"bomb\": \"bomb\"", "\"bomb\": \"anvil\""),
                    "enemies/bomber.json", "unknown falling object id 'anvil'"));
  CHECK(anyContains(errorsWith("waves/m2_storm.json", "\"object\": \"meteor\", \"bearingDeg\": 140",
                               "\"object\": \"comet\", \"bearingDeg\": 140"),
                    "waves/m2_storm.json", "unknown falling object id 'comet'"));
  // A volley of objects that have no spawn height (bombs are only dropped).
  CHECK(anyContains(errorsWith("waves/m2_storm.json", "\"object\": \"meteor\", \"bearingDeg\": 140",
                               "\"object\": \"bomb\", \"bearingDeg\": 140"),
                    "waves/m2_storm.json", "spawnRadius"));
  // A wave with neither lane spawns nor volleys.
  CHECK(anyContains(errorsWith("waves/m1.json", "[ {\"lane\": \"A\", \"enemy\": \"drifter_red\", \"count\": 14} ]",
                               "[]"),
                    "waves/m1.json", "'spawns': must not be empty"));
  // ... but a meteor-only wave is fine.
  const std::vector<std::string> ok =
      errorsWith("waves/m2_storm.json", "[ {\"lane\": \"A\", \"enemy\": \"drifter_red\", \"count\": 10} ]", "[]");
  CHECK_MESSAGE(ok.empty(), joined(ok));
}

}  // TEST_SUITE
