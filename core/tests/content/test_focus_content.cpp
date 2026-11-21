// M2.11 content tests: the rules.json "aim" and "focus" blocks (DESIGN Q33,
// Q34 defaults), the m2_swarm wave set, and loader errors for broken samples.
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

TEST_CASE("M2.11 rules.json aim and focus blocks load with the Q33/Q34 defaults") {
  const ContentDB db = shipped();
  const AimRulesDef& aim = db.rules.aim;
  REQUIRE(aim.offsets.size() == 5);
  CHECK(aim.offsets[0] == doctest::Approx(-60.0f * kDeg));
  CHECK(aim.offsets[2] == doctest::Approx(0.0f));
  CHECK(aim.offsets[4] == doctest::Approx(60.0f * kDeg));
  CHECK(aim.reaimCostFrac == doctest::Approx(0.1f));
  CHECK(aim.reaimBetweenWavesOnly);
  const FocusRulesDef& focus = db.rules.focus;
  CHECK(focus.bonusPerExtra == doctest::Approx(0.25f));
  CHECK(focus.cap == doctest::Approx(0.5f));
  CHECK(focus.radius == doctest::Approx(50.0f));
  CHECK(focus.lensMerge);
  CHECK(focus.preferZone);
  // Aim-mounted towers exist and the Sun Lens is one of them (beam merge).
  const int lens = db.building_index("sun_lens");
  REQUIRE(lens != kNoIndex);
  CHECK(db.buildings[static_cast<size_t>(lens)].aimMount);
}

TEST_CASE("M2.11 rules without aim/focus blocks keep the M1 behaviour (free aim, no bonus)") {
  RulesDef r;
  CHECK(r.aim.offsets.empty());
  CHECK(r.aim.reaimCostFrac == 0.0f);
  CHECK_FALSE(r.aim.reaimBetweenWavesOnly);
  CHECK(r.focus.bonusPerExtra == 0.0f);
  CHECK_FALSE(r.focus.lensMerge);
}

TEST_CASE("M2.11 m2_swarm: a ramp of drifter swarms on both lanes") {
  const ContentDB db = shipped();
  const int set = db.wave_set_index("m2_swarm");
  REQUIRE(set != kNoIndex);
  const WaveSetDef& ws = db.waveSets[static_cast<size_t>(set)];
  REQUIRE(ws.waves.size() >= 10);
  int prev = 0;
  for (const WaveDef& w : ws.waves) {
    int count = 0;
    for (const SpawnDef& sp : w.spawns) {
      count += sp.count;
      CHECK(db.enemies[static_cast<size_t>(sp.enemy)].behavior == EnemyBehavior::Lane);
    }
    CHECK(count > prev);  // every wave is bigger than the last
    prev = count;
  }
}

TEST_CASE("M2.11 the loader rejects broken aim and focus blocks") {
  const std::string from = "\"offsetsDeg\": [-60, -30, 0, 30, 60]";
  CHECK(anyContains(errorsWith("rules.json", from, "\"offsetsDeg\": [-90, 0, 30]"), "rules.json", "offsetsDeg"));
  CHECK(anyContains(errorsWith("rules.json", from, "\"offsetsDeg\": [-30, 30]"), "rules.json", "must include 0"));
  CHECK(anyContains(errorsWith("rules.json", from, "\"offsetsDeg\": [30, 0]"), "rules.json", "ascending"));
  CHECK(anyContains(errorsWith("rules.json", from, "\"offsetsDeg\": []"), "rules.json", "must not be empty"));
  CHECK(anyContains(errorsWith("rules.json", "\"reaimCostFrac\": 0.1", "\"reaimCostFrac\": -1"), "rules.json",
                    "reaimCostFrac"));
  CHECK(anyContains(errorsWith("rules.json", "\"radius\": 50", "\"radius\": 0"), "rules.json", "radius"));
  CHECK(anyContains(errorsWith("rules.json", "\"bonusPerExtra\": 0.25", "\"bonusPerExtra\": -0.1"), "rules.json",
                    "bonusPerExtra"));
}

}  // TEST_SUITE
