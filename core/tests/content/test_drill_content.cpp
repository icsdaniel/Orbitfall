// M2.7 content tests: drill.json, cooler.json and the rules.json
// "instability" block load; their numbers make sense together; the loader
// rejects bad drill/cooler/instability samples.
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "systems/drills.h"
#include "systems/upgrades.h"

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

// Loads the shipped content with `from` replaced by `to` (once) in `file`;
// expects an error naming the file and containing `needle`.
void expectRejected(const std::string& file, const std::string& from, const std::string& to,
                    const std::string& needle) {
  Files files = shippedFiles();
  REQUIRE(files.count(file) == 1);
  std::string& text = files[file];
  const size_t at = text.find(from);
  REQUIRE_MESSAGE(at != std::string::npos, file << " does not contain: " << from);
  REQUIRE_MESSAGE(text.find(from, at + 1) == std::string::npos, file << " contains twice: " << from);
  text.replace(at, from.size(), to);
  ContentDB db;
  std::vector<std::string> errors;
  load_content_from_strings(files, db, errors);
  bool found = false;
  std::string all;
  for (const std::string& e : errors) {
    all += "\n  " + e;
    found = found || (e.find(file) != std::string::npos && e.find(needle) != std::string::npos);
  }
  CHECK_MESSAGE(found, "expected \"" << needle << "\" in " << file << ", got:" << all);
}

const ContentDB& db() {
  static ContentDB content;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, content, errors));
    loaded = true;
  }
  return content;
}

const BuildingDef& building(const char* id) {
  const int i = db().building_index(id);
  REQUIRE_MESSAGE(i != kNoIndex, id);
  return db().buildings[static_cast<size_t>(i)];
}

}  // namespace

TEST_SUITE("content") {

TEST_CASE("M2.7: drill, cooler and instability load from content") {
  const BuildingDef& drill = building("drill");
  CHECK(drill.kind == BuildingKind::Drill);
  CHECK(drill.layer == BuildingLayer::Ground);
  CHECK(drill.drillRate > 0.0f);
  CHECK(drill.instability > 0.0f);
  CHECK(drill.paths.size() == 2);
  const BuildingDef& cooler = building("cooler");
  CHECK(cooler.kind == BuildingKind::Cooler);
  CHECK(cooler.cooling > 0.0f);
  // Coolers radiate best on the night side (DECISIONS M2.7).
  CHECK(cooler.litCoolingMult < 1.0f);
  CHECK(cooler.paths.size() == 2);

  const InstabilityDef& in = db().rules.instability;
  REQUIRE(in.curve.size() >= 2);
  CHECK(in.curve.front().y == 0.0f);  // no drills, no instability
  for (size_t i = 1; i < in.curve.size(); ++i) CHECK(in.curve[i].y >= in.curve[i - 1].y);
  CHECK(in.threshold > 0.0f);
  CHECK(in.overheatSeconds > 0.0f);
}

TEST_CASE("M2.7: shipped numbers: a few drills are safe, many overheat, one cooler buys one more") {
  const BuildingDef& drill = building("drill");
  const BuildingDef& cooler = building("cooler");
  const InstabilityDef& in = db().rules.instability;
  int safe = 0;  // drills that run without ever heating
  while (instabilityLevel(in, drill.instability * static_cast<float>(safe + 1), 0.0f) <= in.threshold) ++safe;
  CHECK(safe >= 2);
  CHECK(safe <= 4);
  int cooled = 0;  // the same with one cooler on the night side
  while (instabilityLevel(in, drill.instability * static_cast<float>(cooled + 1), cooler.cooling) <= in.threshold)
    ++cooled;
  CHECK(cooled == safe + 1);
}

TEST_CASE("M2.7: every drill and cooler upgrade tier keeps its stats positive") {
  for (const char* id : {"drill", "cooler"}) {
    const BuildingDef& b = building(id);
    for (int t0 = 0; t0 <= 4; ++t0) {
      for (int t1 = 0; t1 <= 4; ++t1) {
        const uint8_t tiers[kUpgradePaths] = {static_cast<uint8_t>(t0), static_cast<uint8_t>(t1)};
        const BuildingStats s = effectiveStats(b, tiers);
        if (b.kind == BuildingKind::Drill) {
          CHECK(s.income > 0.0f);
          CHECK(s.instability > 0.0f);
        } else {
          CHECK(s.cooling > 0.0f);
        }
      }
    }
  }
  // Deep Bore earns more; Stable Core weighs less in the curve.
  const BuildingDef& drill = building("drill");
  const uint8_t none[kUpgradePaths] = {0, 0}, bore[kUpgradePaths] = {4, 2}, calm[kUpgradePaths] = {2, 4};
  CHECK(effectiveStats(drill, bore).income > effectiveStats(drill, none).income);
  CHECK(effectiveStats(drill, calm).instability < effectiveStats(drill, none).instability);
}

TEST_CASE("M2.7: the loader rejects bad drill, cooler and instability data") {
  expectRejected("buildings/drill.json", "\"drillRate\": 2,", "", "missing field 'drillRate'");
  expectRejected("buildings/drill.json", "\"instability\": 1,", "\"instability\": -1,", "must not be negative");
  expectRejected("buildings/cooler.json", "\"cooling\": 0.4,", "", "missing field 'cooling'");
  expectRejected("buildings/cooler.json", "\"stat\": \"cooling\", \"op\": \"add\", \"value\": 0.1 }",
                 "\"stat\": \"damage\", \"op\": \"add\", \"value\": 0.1 }", "only 'cooling' applies to coolers");
  expectRejected("buildings/drill.json", "\"name\": \"Hard Bit\", \"cost\": 42, \"mods\": [{ \"stat\": \"income\"",
                 "\"name\": \"Hard Bit\", \"cost\": 42, \"mods\": [{ \"stat\": \"cooling\"",
                 "only 'income' and 'instability' apply to drills");
  expectRejected("buildings/gatling.json", "{ \"stat\": \"fireRate\", \"op\": \"mul\", \"value\": 1.3 }] },\n      { \"name\": \"Twin",
                 "{ \"stat\": \"instability\", \"op\": \"mul\", \"value\": 1.3 }] },\n      { \"name\": \"Twin",
                 "'instability' only applies to drills");
  expectRejected("rules.json", "[3, 0.8], [4, 1.2]", "[3, 0.8], [4, 0.5]", "must not fall");
  expectRejected("rules.json", "[3, 0.8], [4, 1.2]", "[3, 0.8], [3, 1.2]", "strictly ascending");
  expectRejected("rules.json", "\"threshold\": 1.0,", "\"threshold\": 0,", "threshold");
}

}  // TEST_SUITE
