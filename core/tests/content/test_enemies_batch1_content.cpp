// M2.5 content tests: the batch 1 enemies (Brick, Phantom, Bubble, Ember,
// Spore, Splitter) load with the readability rules (shape = movement class,
// colour tier = health, trait bits for the cues), the Regrow and Split data
// is validated, and the m2_bestiary demo wave set features all of them.
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

TEST_CASE("M2.5 enemies batch 1: each shipped enemy follows the readability rules") {
  const ContentDB db = shipped();
  struct Row {
    const char* id;
    Shape shape;      // shape = movement class
    uint32_t traits;  // outline/badge = trait (must carry at least these)
  };
  const Row rows[] = {
      {"brick", Shape::Square, kTraitArmored},
      {"phantom", Shape::Circle, kTraitCamo},
      {"bubble", Shape::Circle, kTraitShielded},
      {"ember", Shape::Triangle, kTraitFast | kTraitHeatResistant},
      {"spore", Shape::Circle, kTraitRegrow | kTraitLayered},
      {"spore_blue", Shape::Circle, kTraitRegrow | kTraitLayered},
      {"spore_red", Shape::Circle, kTraitRegrow},
      {"splitter", Shape::Circle, kTraitSplitter},
  };
  for (const Row& r : rows) {
    CAPTURE(r.id);
    const int i = db.enemy_index(r.id);
    REQUIRE(i != kNoIndex);
    const EnemyDef& e = db.enemies[static_cast<size_t>(i)];
    CHECK(e.behavior == EnemyBehavior::Lane);
    CHECK(e.visual.shape == r.shape);
    CHECK((e.traits & r.traits) == r.traits);
    CHECK(e.visual.colorTier >= 0);  // colour tier = health
    CHECK_FALSE(e.hpBar);            // small enemies never show HP bars
    CHECK_FALSE(e.shadowCaster);
  }
  // A tougher unit sits on a higher colour tier than a red drifter.
  const int red = db.enemies[static_cast<size_t>(db.enemy_index("drifter_red"))].visual.colorTier;
  CHECK(db.enemies[static_cast<size_t>(db.enemy_index("brick"))].visual.colorTier > red);
}

TEST_CASE("M2.5 Spore chain: three layers that regrow, Splitter splits into 2 to 4 drifters") {
  const ContentDB db = shipped();
  const int spore = db.enemy_index("spore"), blue = db.enemy_index("spore_blue"),
            sred = db.enemy_index("spore_red");
  REQUIRE(spore != kNoIndex);
  CHECK(db.layer_chain(spore) == std::vector<int>{spore, blue, sred});
  for (int i : {spore, blue, sred}) CHECK(db.enemies[static_cast<size_t>(i)].regrowSeconds == doctest::Approx(2.5));
  const EnemyDef& s = db.enemies[static_cast<size_t>(db.enemy_index("splitter"))];
  CHECK(s.splitInto == db.enemy_index("drifter_red"));
  CHECK(s.splitCount == 3);
  CHECK(s.splitSpread == doctest::Approx(14));
  const EnemyDef& b = db.enemies[static_cast<size_t>(db.enemy_index("bubble"))];
  CHECK(b.shield == doctest::Approx(3));
}

TEST_CASE("M2.5 demo wave set m2_bestiary features every new enemy, M1 waves untouched") {
  const ContentDB db = shipped();
  const int set = db.wave_set_index("m2_bestiary");
  REQUIRE(set != kNoIndex);
  const WaveSetDef& ws = db.waveSets[static_cast<size_t>(set)];
  CHECK(ws.waves.size() == 4);
  for (const char* id : {"brick", "phantom", "bubble", "ember", "spore", "splitter"}) {
    CAPTURE(id);
    bool found = false;
    for (const WaveDef& w : ws.waves)
      for (const SpawnDef& s : w.spawns) found = found || s.enemy == db.enemy_index(id);
    CHECK(found);
  }
  CHECK(db.wave_set_index("m1") == 0);
  const WaveSetDef& m1 = db.waveSets[0];
  for (const WaveDef& w : m1.waves)
    for (const SpawnDef& s : w.spawns) {
      const std::string& id = db.enemies[static_cast<size_t>(s.enemy)].id;
      CHECK((id.rfind("drifter", 0) == 0 || id == "dart" || id == "eclipser"));
    }
}

TEST_CASE("M2.5 loader rejects bad regrow and split data") {
  SUBCASE("regrow trait without a regrow block") {
    CHECK(anyContains(errorsWith("enemies/spore_red.json", "\"regrow\": { \"seconds\": 2.5 },", ""),
                      "enemies/spore_red.json", "'regrow' needs a 'regrow' block"));
  }
  SUBCASE("regrow seconds must be positive") {
    CHECK(anyContains(errorsWith("enemies/spore.json", "\"seconds\": 2.5", "\"seconds\": 0"),
                      "enemies/spore.json", "seconds"));
  }
  SUBCASE("a single-layer enemy no chain pops into has nothing to regrow") {
    CHECK(anyContains(errorsWith("enemies/phantom.json", "\"traits\": [\"camo\"],",
                                 "\"traits\": [\"camo\"], \"regrow\": { \"seconds\": 1 },"),
                      "enemies/phantom.json", "nothing to regrow"));
  }
  SUBCASE("splitter trait without a split block") {
    CHECK(anyContains(errorsWith("enemies/splitter.json",
                                 "\"split\": { \"into\": \"drifter_red\", \"count\": 3, \"spread\": 14 },", ""),
                      "enemies/splitter.json", "'splitter' needs a 'split' block"));
  }
  SUBCASE("unknown child") {
    CHECK(anyContains(errorsWith("enemies/splitter.json", "\"into\": \"drifter_red\"", "\"into\": \"blob\""),
                      "enemies/splitter.json", "unknown enemy id 'blob'"));
  }
  SUBCASE("count out of range") {
    CHECK(anyContains(errorsWith("enemies/splitter.json", "\"count\": 3", "\"count\": 0"),
                      "enemies/splitter.json", "count"));
    CHECK(anyContains(errorsWith("enemies/splitter.json", "\"count\": 3", "\"count\": 9"),
                      "enemies/splitter.json", "count"));
  }
  SUBCASE("a split that leads back to itself") {
    CHECK(anyContains(errorsWith("enemies/splitter.json", "\"into\": \"drifter_red\"", "\"into\": \"splitter\""),
                      "enemies/splitter.json", "leads back"));
  }
}

}  // TEST_SUITE
