// M2.3 content tests: content/damage.json (soft-counter matrix + trait
// legend) loads, matches PLAN §5.5, and no multiplier anywhere in the shipped
// content can go below 0.25 (no hard counters).
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "systems/combat.h"

#ifndef ORBITFALL_CONTENT_DIR
#define ORBITFALL_CONTENT_DIR "../content"
#endif

using namespace orbitfall;

namespace {

using Files = std::map<std::string, std::string>;

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

// True when loading fails with an error naming `file` and containing `needle`.
bool rejected(const Files& files, const std::string& file, const std::string& needle) {
  ContentDB db;
  std::vector<std::string> errors;
  if (load_content_from_strings(files, db, errors)) return false;
  for (const std::string& e : errors)
    if (e.find(file) != std::string::npos && e.find(needle) != std::string::npos) return true;
  MESSAGE("errors were:" << joined(errors));
  return false;
}

ContentDB shippedDb() {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE_MESSAGE(load_content(ORBITFALL_CONTENT_DIR, db, errors), joined(errors));
  return db;
}

int bitIndex(uint32_t bit) {
  int i = 0;
  while ((1u << i) != bit) ++i;
  return i;
}

const TraitDef& trait(const ContentDB& db, uint32_t bit) { return db.damage.traits[bitIndex(bit)]; }
float mult(const TraitDef& t, DamageType type) { return t.mult[static_cast<int>(type)]; }

}  // namespace

TEST_SUITE("content") {

TEST_CASE("damage.json: the soft-counter matrix matches PLAN 5.5") {
  const ContentDB db = shippedDb();
  const TraitDef& armored = trait(db, kTraitArmored);
  CHECK(armored.id == "armored");
  CHECK(armored.name == "Armored");
  CHECK(mult(armored, DamageType::Kinetic) == doctest::Approx(0.5));
  CHECK(mult(armored, DamageType::Explosive) == doctest::Approx(1.0));
  const TraitDef& heat = trait(db, kTraitHeatResistant);
  CHECK(mult(heat, DamageType::Energy) == doctest::Approx(0.5));
  CHECK(mult(heat, DamageType::Kinetic) == doctest::Approx(1.0));
  CHECK(db.damage.shieldMult[static_cast<int>(DamageType::Emp)] > 1.0f);

  // The eight M2.3 traits each have a legend entry for the codex.
  for (uint32_t bit : {kTraitCamo, kTraitArmored, kTraitShielded, kTraitFast, kTraitLayered,
                       kTraitRegrow, kTraitSplitter, kTraitHeatResistant}) {
    const TraitDef& t = trait(db, bit);
    CAPTURE(t.id);
    CHECK_FALSE(t.name.empty());
    CHECK_FALSE(t.cue.empty());
    CHECK_FALSE(t.effect.empty());
    CHECK_FALSE(t.easierWith.empty());
  }
  // Every trait bit has a name, so the codex never shows a blank.
  for (int bit = 0; bit < kTraitCount; ++bit) CHECK_FALSE(db.damage.traits[bit].name.empty());
}

TEST_CASE("no multiplier below 0.25 anywhere in the shipped content") {
  const ContentDB db = shippedDb();
  CHECK(db.rules.minDamageMult >= 0.25f);
  for (int bit = 0; bit < kTraitCount; ++bit)
    for (int t = 0; t < kDamageTypeCount; ++t) {
      CAPTURE(db.damage.traits[bit].id);
      CHECK(db.damage.traits[bit].mult[t] >= 0.25f);
    }
  for (int t = 0; t < kDamageTypeCount; ++t) CHECK(db.damage.shieldMult[t] >= 0.25f);
  // Every tower against every enemy, at every range, and every damage type
  // against every enemy and every combination of traits.
  for (const EnemyDef& e : db.enemies) {
    CAPTURE(e.id);
    for (int t = 0; t < kDamageTypeCount; ++t) {
      CHECK(e.damageMult[t] >= 0.25f);
      CHECK(typeMultiplier(db, e, static_cast<DamageType>(t)) >= 0.25f);
    }
    for (const BuildingDef& b : db.buildings) {
      if (!b.hasProjectile) continue;
      CAPTURE(b.id);
      for (float f = 0.0f; f <= 1.0f; f += 0.05f) CHECK(damageMultiplier(db, b, e, f) >= 0.25f);
    }
  }
  EnemyDef any = db.enemies[0];
  for (uint32_t traits = 0; traits < (1u << kTraitCount); ++traits) {
    any.traits = traits;
    for (int t = 0; t < kDamageTypeCount; ++t)
      REQUIRE(typeMultiplier(db, any, static_cast<DamageType>(t)) >= 0.25f);
  }
}

TEST_CASE("damage.json: rejects multipliers below the floor and unknown names") {
  SUBCASE("trait row") {
    Files files = shippedFiles();
    edit(files, "damage.json", "\"kinetic\": 0.5", "\"kinetic\": 0.2");
    CHECK(rejected(files, "damage.json", "below the damage multiplier floor"));
  }
  SUBCASE("immunity") {
    Files files = shippedFiles();
    edit(files, "damage.json", "\"energy\": 0.5", "\"energy\": 0");
    CHECK(rejected(files, "damage.json", "below the damage multiplier floor"));
  }
  SUBCASE("shield row") {
    Files files = shippedFiles();
    edit(files, "damage.json", "\"emp\": 2", "\"emp\": 2, \"kinetic\": 0.1");
    CHECK(rejected(files, "damage.json", "below the damage multiplier floor"));
  }
  SUBCASE("unknown trait") {
    Files files = shippedFiles();
    edit(files, "damage.json", "\"camo\": {", "\"sneaky\": {");
    CHECK(rejected(files, "damage.json", "unknown trait 'sneaky'"));
  }
  SUBCASE("unknown damage type") {
    Files files = shippedFiles();
    edit(files, "damage.json", "\"emp\": 2", "\"plasma\": 2");
    CHECK(rejected(files, "damage.json", "unknown damage type 'plasma'"));
  }
}

TEST_CASE("M1 content is unchanged by M2.3: Kinetic and Explosive towers, 1x on every enemy") {
  const ContentDB db = shippedDb();
  for (const BuildingDef& b : db.buildings) {
    if (!b.hasProjectile) continue;
    // M2.6 towers have their own falloff curves and types (test_towers_content.cpp).
    if (b.id != "gatling" && b.id != "cannon") continue;
    for (const char* id : {"drifter_red", "drifter_blue", "drifter_green", "dart", "eclipser"}) {
      const EnemyDef& e = db.enemies[static_cast<size_t>(db.enemy_index(id))];
      CAPTURE(b.id);
      CAPTURE(id);
      CHECK(damageMultiplier(db, b, e, 0.5f) == doctest::Approx(1.0f));
      CHECK(e.shield == 0.0f);
    }
  }
  // Layered is derived from the layer count, not authored.
  CHECK((db.enemies[static_cast<size_t>(db.enemy_index("drifter_green"))].traits & kTraitLayered) != 0);
  CHECK((db.enemies[static_cast<size_t>(db.enemy_index("drifter_red"))].traits & kTraitLayered) == 0);
}

TEST_CASE("enemy fields added by M2.3: shield and hpBarSegments") {
  Files files = shippedFiles();
  edit(files, "enemies/dart.json", "\"bounty\": 1,", "\"bounty\": 1, \"shield\": 3,");
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE_MESSAGE(load_content_from_strings(files, db, errors), joined(errors));
  const EnemyDef& d = db.enemies[static_cast<size_t>(db.enemy_index("dart"))];
  CHECK(d.shield == doctest::Approx(3));
  CHECK((d.traits & kTraitShielded) != 0);  // a shield amount implies the trait
  const EnemyDef& ecl = db.enemies[static_cast<size_t>(db.enemy_index("eclipser"))];
  CHECK(ecl.hpBar);
  CHECK(ecl.hpBarSegments == 4);

  SUBCASE("the trait without an amount is rejected") {
    Files bad = shippedFiles();
    edit(bad, "enemies/dart.json", "\"traits\": [\"fast\"]", "\"traits\": [\"fast\", \"shielded\"]");
    CHECK(rejected(bad, "enemies/dart.json", "needs a 'shield' amount"));
  }
  SUBCASE("content without a damage.json still loads, with a neutral matrix") {
    Files none = shippedFiles();
    none.erase("damage.json");
    ContentDB plain;
    std::vector<std::string> errs;
    REQUIRE_MESSAGE(load_content_from_strings(none, plain, errs), joined(errs));
    CHECK(plain.damage.traits[1].mult[0] == doctest::Approx(1.0));
    CHECK(plain.damage.traits[1].name == "armored");
  }
}

}  // TEST_SUITE
