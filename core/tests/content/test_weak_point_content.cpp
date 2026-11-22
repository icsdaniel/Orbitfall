// M2.4 content tests: enemies/bulwark.json, buildings/lance.json,
// waves/trial_bulwark.json and the weakPoint / precision / spawnJitterDeg /
// weakWindowDeg fields (PLAN §5.5, §5.6, DECISIONS Q1).
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/mathx.h"
#include "systems/combat.h"

#ifndef ORBITFALL_CONTENT_DIR
#define ORBITFALL_CONTENT_DIR "../content"
#endif

using namespace orbitfall;

namespace {

using Files = std::map<std::string, std::string>;
constexpr float kDegree = kPi / 180.0f;

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

void edit(Files& files, const std::string& file, const std::string& from, const std::string& to) {
  REQUIRE_MESSAGE(files.count(file) == 1, file);
  std::string& text = files[file];
  const size_t at = text.find(from);
  REQUIRE_MESSAGE(at != std::string::npos, file << " does not contain: " << from);
  text.replace(at, from.size(), to);
}

ContentDB loaded(const Files& files) {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE_MESSAGE(load_content_from_strings(files, db, errors), joined(errors));
  return db;
}

bool rejected(const Files& files, const std::string& file, const std::string& needle) {
  ContentDB db;
  std::vector<std::string> errors;
  if (load_content_from_strings(files, db, errors)) return false;
  for (const std::string& e : errors)
    if (e.find(file) != std::string::npos && e.find(needle) != std::string::npos) return true;
  MESSAGE("errors were:" << joined(errors));
  return false;
}

}  // namespace

TEST_CASE("bulwark.json: a weak point with body about 25% and weak side 3 to 4x") {
  const ContentDB db = loaded(shippedFiles());
  const int i = db.enemy_index("bulwark");
  REQUIRE(i != kNoIndex);
  const EnemyDef& b = db.enemies[static_cast<size_t>(i)];
  CHECK(b.hasWeakPoint);
  CHECK((b.traits & kTraitBulwark) != 0);  // the wedge marker's trait bit
  CHECK(b.weakPoint.mult >= 3.0f);
  CHECK(b.weakPoint.mult <= 4.0f);
  CHECK(b.weakPoint.bodyMult == doctest::Approx(0.25f));
  CHECK(b.weakPoint.window == doctest::Approx(8.0f * kDegree));  // from rules.weakWindowDeg
  CHECK(b.shape == Shape::Square);  // a plate; shape = movement class (tank)
  CHECK_FALSE(b.hpBar);             // not carrier-size (readability rule 7)
  CHECK(b.laneJitter > db.rules.spawnJitter);  // comes in spread wide: lining up is a decision
  CHECK(db.rules.weakPointPrecisionOnly);  // only the Lance (precision) reaches the weak side
  // Every shipped tower: body never 0 (>= the floor), weak side at least 3x
  // whatever falloff point it hits at.
  for (const BuildingDef& t : db.buildings) {
    if (!t.hasProjectile) continue;
    CAPTURE(t.id);
    for (float f = 0.0f; f <= 1.0f; f += 0.25f) {
      CHECK(hitMultiplier(db, t, b, f, false) >= 0.25f);
      CHECK(hitMultiplier(db, t, b, f, true) >= 3.0f * falloffAt(t, f) - 1e-4f);
    }
  }
  // The trait legend has a Bulwark row for the codex.
  CHECK(db.damage.traits[10].name == "Bulwark");
  CHECK_FALSE(db.damage.traits[10].effect.empty());
}

TEST_CASE("lance.json: narrow, cheap, precision, Pierce, weak against everything else") {
  const ContentDB db = loaded(shippedFiles());
  const int i = db.building_index("lance");
  REQUIRE(i != kNoIndex);
  const BuildingDef& l = db.buildings[static_cast<size_t>(i)];
  CHECK(l.kind == BuildingKind::Tower);
  CHECK(l.arc == doctest::Approx(6.0f * kDegree));
  CHECK(l.precision);
  CHECK_FALSE(l.aimMount);  // placement-neutral: no mount, no slot bonuses
  CHECK(l.projectile.type == DamageType::Pierce);
  float cheapest = 1e9f;
  for (const BuildingDef& b : db.buildings)
    if (b.kind == BuildingKind::Tower && b.id != l.id && b.cost < cheapest) cheapest = b.cost;
  CHECK(l.cost < cheapest);
  // Weak against everything else: less damage per second than any other
  // damage tower (the Cryo's slow is its job, not its damage; M2.6).
  const float dps = l.projectile.damage * l.fireRate;
  for (const BuildingDef& b : db.buildings) {
    if (!b.hasProjectile || b.id == l.id || b.projectile.slow > 0.0f) continue;
    CAPTURE(b.id);
    CHECK(dps < b.projectile.damage * b.fireRate);
  }
  // The precision towers are the Lance and the Railgun (the dear, long-range
  // second answer to the Bulwark, M2.6): both fire Pierce.
  int precision = 0;
  for (const BuildingDef& b : db.buildings) {
    if (!b.precision) continue;
    CAPTURE(b.id);
    CHECK(b.projectile.type == DamageType::Pierce);
    ++precision;
  }
  CHECK(precision == 2);
  const int r = db.building_index("railgun");
  REQUIRE(r != kNoIndex);
  CHECK(db.buildings[static_cast<size_t>(r)].precision);
  CHECK(db.buildings[static_cast<size_t>(r)].cost > l.cost);
}

TEST_CASE("waves: M1 has no Bulwarks; the trial wave set does") {
  const ContentDB db = loaded(shippedFiles());
  const int bulwark = db.enemy_index("bulwark");
  const int m1 = db.wave_set_index("m1");
  const int trial = db.wave_set_index("trial_bulwark");
  REQUIRE(m1 == 0);  // the game's default wave set
  REQUIRE(trial != kNoIndex);
  for (const WaveDef& w : db.waveSets[static_cast<size_t>(m1)].waves)
    for (const SpawnDef& s : w.spawns) CHECK(s.enemy != bulwark);
  int count = 0;
  for (const WaveDef& w : db.waveSets[static_cast<size_t>(trial)].waves)
    for (const SpawnDef& s : w.spawns) count += s.enemy == bulwark ? s.count : 0;
  CHECK(count > 0);
}

TEST_CASE("weakPoint fields: overrides and validation") {
  SUBCASE("the window can be set per enemy; rules set the default") {
    Files files = shippedFiles();
    edit(files, "enemies/bulwark.json", "\"bodyMult\": 0.25", "\"bodyMult\": 0.25, \"windowDeg\": 5");
    edit(files, "rules.json", "\"weakWindowDeg\": 8", "\"weakWindowDeg\": 10");
    const ContentDB db = loaded(files);
    CHECK(db.enemies[static_cast<size_t>(db.enemy_index("bulwark"))].weakPoint.window ==
          doctest::Approx(5.0f * kDegree));
    CHECK(db.rules.weakWindow == doctest::Approx(10.0f * kDegree));
  }
  SUBCASE("the precision-only rule can be switched off in rules") {
    Files files = shippedFiles();
    edit(files, "rules.json", "\"weakPointPrecisionOnly\": true", "\"weakPointPrecisionOnly\": false");
    CHECK_FALSE(loaded(files).rules.weakPointPrecisionOnly);
  }
  SUBCASE("a body multiplier under the floor is rejected (never 0)") {
    Files files = shippedFiles();
    edit(files, "enemies/bulwark.json", "\"bodyMult\": 0.25", "\"bodyMult\": 0");
    CHECK(rejected(files, "enemies/bulwark.json", "below the damage multiplier floor"));
  }
  SUBCASE("a weak-point multiplier under 1 is rejected") {
    Files files = shippedFiles();
    edit(files, "enemies/bulwark.json", "\"mult\": 3.5", "\"mult\": 0.5");
    CHECK(rejected(files, "enemies/bulwark.json", "'mult'"));
  }
  SUBCASE("the bulwark trait needs a weak point") {
    Files files = shippedFiles();
    edit(files, "enemies/bulwark.json", "\"weakPoint\": { \"mult\": 3.5, \"bodyMult\": 0.25 },", "");
    CHECK(rejected(files, "enemies/bulwark.json", "'bulwark' needs a 'weakPoint'"));
  }
  SUBCASE("a weak point implies the trait") {
    Files files = shippedFiles();
    edit(files, "enemies/bulwark.json", "\"traits\": [\"bulwark\"]", "\"traits\": []");
    const ContentDB db = loaded(files);
    CHECK((db.enemies[static_cast<size_t>(db.enemy_index("bulwark"))].traits & kTraitBulwark) != 0);
  }
  SUBCASE("spawnJitterDeg is for lane enemies only") {
    Files files = shippedFiles();
    edit(files, "enemies/eclipser.json", "\"bounty\": 15,", "\"bounty\": 15, \"spawnJitterDeg\": 4,");
    CHECK(rejected(files, "enemies/eclipser.json", "only for lane enemies"));
  }
  SUBCASE("enemies without the field keep the rules jitter") {
    const ContentDB db = loaded(shippedFiles());
    CHECK(db.enemies[static_cast<size_t>(db.enemy_index("drifter_red"))].laneJitter < 0.0f);
  }
}
