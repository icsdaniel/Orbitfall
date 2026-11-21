// M2.6 content tests: the six batch-1 towers (Flak, Railgun, Cryo, Radar,
// Amplifier, Sun Lens) load with their numbers, 2 x 4 upgrade paths and
// falloff curves, and the loader rejects malformed M2.6 fields.
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


const BuildingDef& def(const ContentDB& db, const char* id) {
  const int i = db.building_index(id);
  REQUIRE_MESSAGE(i != kNoIndex, id);
  return db.buildings[static_cast<size_t>(i)];
}

}  // namespace

TEST_CASE("M2.6 towers: every batch-1 building loads with 2 paths x 4 tiers") {
  const ContentDB db = shippedDb();
  for (const char* id : {"flak", "railgun", "cryo", "radar", "amplifier", "sun_lens"}) {
    CAPTURE(id);
    const BuildingDef& b = def(db, id);
    REQUIRE(b.paths.size() == 2);
    for (const UpgradePathDef& p : b.paths) CHECK(p.tiers.size() == 4);
    CHECK(b.cost > 0.0f);
    CHECK(!b.name.empty());
  }
}

TEST_CASE("M2.6 towers: shipped numbers match their roles") {
  const ContentDB db = shippedDb();
  const BuildingDef& flak = def(db, "flak");
  CHECK(flak.projectile.type == DamageType::Explosive);
  CHECK(flak.arc > def(db, "gatling").arc);  // wide arc
  CHECK(falloffAt(flak, 0.0f) > 1.0f);       // strong close...
  CHECK(falloffAt(flak, 1.0f) < 1.0f);       // ...weak far
  const BuildingDef& rail = def(db, "railgun");
  CHECK(rail.projectile.type == DamageType::Pierce);
  CHECK(rail.projectile.pierce >= 1);
  CHECK(rail.range > def(db, "cannon").range);            // long range
  CHECK(falloffAt(rail, 0.0f) == falloffAt(rail, 1.0f));  // constant
  const BuildingDef& cryo = def(db, "cryo");
  CHECK(cryo.projectile.type == DamageType::Cryo);
  CHECK(cryo.projectile.slow > 0.0f);
  CHECK(cryo.projectile.slowSeconds > 0.0f);
  const BuildingDef& radar = def(db, "radar");
  CHECK(radar.kind == BuildingKind::Support);
  bool detects = false;
  for (const AuraDef& a : radar.auras) detects = detects || (a.stat == AuraStat::Detect && a.amount >= 1.0f);
  CHECK(detects);
  const BuildingDef& amp = def(db, "amplifier");
  bool damage = false, shield = false;
  for (const AuraDef& a : amp.auras) {
    damage = damage || (a.stat == AuraStat::Damage && a.amount > 0.0f);
    shield = shield || (a.stat == AuraStat::ShieldDamage && a.amount > 0.0f);
  }
  CHECK(damage);
  CHECK(shield);
  const BuildingDef& lens = def(db, "sun_lens");
  CHECK(lens.projectile.kind == ProjectileKind::Beam);
  CHECK(lens.projectile.type == DamageType::Energy);
  CHECK(lens.arc == doctest::Approx(35.0f * 3.14159265f / 180.0f));
  CHECK(lens.beam.channelFloor == doctest::Approx(0.2f));
  REQUIRE(lens.hasActive);
  CHECK(lens.active.id == "overcharge");
  CHECK(lens.active.damageMult > 1.0f);
  CHECK(db.damage.traits[0].missChance == doctest::Approx(0.5f));  // camo
}

TEST_CASE("M2.6 loader: malformed batch-1 fields are rejected") {
  SUBCASE("aura stat on a building without auras") {
    Files files = shippedFiles();
    edit(files, "buildings/flak.json", "\"stat\": \"splash\", \"op\": \"mul\", \"value\": 1.2 }",
         "\"stat\": \"auraRadius\", \"op\": \"add\", \"value\": 1 }");
    CHECK(rejected(files, "flak.json", "need the building to have auras"));
  }
  SUBCASE("active stat without an active") {
    Files files = shippedFiles();
    edit(files, "buildings/cryo.json", "\"stat\": \"slow\", \"op\": \"add\", \"value\": 0.08 }",
         "\"stat\": \"activePower\", \"op\": \"add\", \"value\": 1 }");
    CHECK(rejected(files, "cryo.json", "need an 'active'"));
  }
  SUBCASE("tower stat on a support building") {
    Files files = shippedFiles();
    edit(files, "buildings/radar.json", "\"stat\": \"auraRadius\", \"op\": \"add\", \"value\": 1 }",
         "\"stat\": \"damage\", \"op\": \"add\", \"value\": 1 }");
    CHECK(rejected(files, "radar.json", "support buildings only take aura stats"));
  }
  SUBCASE("slow above the cap") {
    Files files = shippedFiles();
    edit(files, "buildings/cryo.json", "\"slow\": 0.4", "\"slow\": 0.95");
    CHECK(rejected(files, "cryo.json", "slow"));
  }
  SUBCASE("unknown aura stat") {
    Files files = shippedFiles();
    edit(files, "buildings/amplifier.json", "\"shieldDamage\"", "\"shieldBreak\"");
    CHECK(rejected(files, "amplifier.json", "unknown value 'shieldBreak'"));
  }
}
