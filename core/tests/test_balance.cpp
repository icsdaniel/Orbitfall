// Balance regression suite (BRIEF §8.2 targets) on the shipped content: four
// deterministic strategy bots (core/bots/bots.h) play whole runs. If a content
// change breaks one of these, either the change or the target is wrong; say
// which in the PR.
//
// The outcome table:  ./build/core_tests --no-skip -tc="balance report"
// (ORBITFALL_BALANCE_CONTENT=<dir> plays another content folder,
// ORBITFALL_BALANCE_PLANET=<id> another home planet, M2.8.)
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "bots/bots.h"
#include "content/content_loader.h"
#include "doctest/doctest.h"

using namespace orbitfall;
using namespace orbitfall_bots;

namespace {

const ContentDB& shippedContent() {
  static ContentDB db;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
    loaded = true;
  }
  return db;
}

const Strategy kAll[] = {Strategy::Never, Strategy::Towers, Strategy::Chaser, Strategy::Sensible,
                         Strategy::Upgrader};

// M2.8: one run on a home planet (kNoIndex = the wave set's own).
RunResult playOn(const ContentDB& db, Strategy s, uint32_t seed, int planet, bool trace = false) {
  RunOptions options;
  options.seed = seed;
  options.trace = trace;
  options.planet = planet;
  return play(db, s, options);
}

}  // namespace

TEST_CASE("balance report" * doctest::skip()) {
  ContentDB other;
  const ContentDB* db = &shippedContent();
  if (const char* dir = std::getenv("ORBITFALL_BALANCE_CONTENT")) {
    std::vector<std::string> errors;
    REQUIRE(load_content(dir, other, errors));
    db = &other;
  }
  int planet = kNoIndex;
  if (const char* id = std::getenv("ORBITFALL_BALANCE_PLANET")) {
    planet = db->planet_index(id);
    REQUIRE_MESSAGE(planet != kNoIndex, id);
  }
  std::printf("\n| Strategy | Result | Waves survived | HP left | Stardust earned (panels) | Leaks A / B | Wave time | Towers / panels | Upgrade tiers |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|\n");
  for (Strategy s : kAll) {
    const RunResult r = playOn(*db, s, 1, planet);
    std::printf("| %s | %s | %u | %.0f / %.0f | %.0f (%.0f) | %u / %u | %.0f s | %u / %u | %u |\n", strategyName(s),
                r.phase == Phase::Won ? "won" : (r.phase == Phase::Lost ? "lost" : "?"), r.wavesCleared,
                r.hp, r.hpMax, r.earned, r.fromPanels, r.leaks[0], r.leaks[1], r.waveSeconds, r.towers,
                r.panels, r.upgrades);
  }
  if (std::getenv("ORBITFALL_BALANCE_TRACE")) {
    for (Strategy s : kAll) {
      std::printf("\n  %s\n", strategyName(s));
      const RunResult r = playOn(*db, s, 1, planet, true);
      std::printf("    leaks A/B per wave:");
      for (uint32_t wave = 0; wave < 8; ++wave) std::printf(" %u/%u", r.waveLeaks[wave][0], r.waveLeaks[wave][1]);
      std::printf("\n");
    }
  }
  std::printf("\nseeds 2..6 (waves survived, HP left):\n");
  for (Strategy s : kAll) {
    std::printf("  %-36s", strategyName(s));
    for (uint32_t seed = 2; seed <= 6; ++seed) {
      const RunResult r = playOn(*db, s, seed, planet);
      std::printf("  %u/%.0f", r.wavesCleared, r.hp);
    }
    std::printf("\n");
  }
}

TEST_CASE("balance: never building loses by wave 3") {
  const RunResult r = play(shippedContent(), Strategy::Never);
  CHECK(r.phase == Phase::Lost);
  CHECK(r.wavesCleared <= 3);
  CHECK(r.wavesCleared >= 1);  // wave 1 alone is survivable: room to learn
  CHECK(r.kills == 0);
}

TEST_CASE("balance: ignoring the sun (towers only, never rotating) loses by wave 6") {
  for (uint32_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const RunResult r = play(shippedContent(), Strategy::Towers, seed);
    CHECK(r.phase == Phase::Lost);
    CHECK(r.wavesCleared <= 5);   // falls in wave 6 at the latest
    CHECK(r.wavesCleared >= 3);   // but towers alone do carry the early waves
    CHECK(r.panels == 0);
    CHECK(r.fromPanels == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(r.rejected == 0);
  }
}

TEST_CASE("balance: only chasing the sun leaks heavily, one lane at a time") {
  const RunResult sensible = play(shippedContent(), Strategy::Sensible);
  const RunResult r = play(shippedContent(), Strategy::Chaser);
  CHECK(r.phase != Phase::Won);
  CHECK(r.fromPanels > 300.0f);  // the light did pay
  CHECK(r.leaks[0] + r.leaks[1] >= 30);
  CHECK(r.leaks[0] + r.leaks[1] >= 10 * (sensible.leaks[0] + sensible.leaks[1]));
  // In its worst wave one lane takes most of the landings: the towers face
  // wherever the sun is, not where the enemies come from.
  uint32_t worst = 0;
  for (uint32_t wave = 1; wave < 16; ++wave) {
    if (r.waveLeaks[wave][0] + r.waveLeaks[wave][1] > r.waveLeaks[worst][0] + r.waveLeaks[worst][1]) worst = wave;
  }
  const uint32_t a = r.waveLeaks[worst][0], b = r.waveLeaks[worst][1];
  CHECK((a > b ? a : b) >= 3 * (a > b ? b : a) / 2);
}

TEST_CASE("balance: a sensible strategy wins wave 8 with HP to spare in 3 to 4 minutes") {
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    CAPTURE(seed);
    const RunResult r = play(shippedContent(), Strategy::Sensible, seed);
    CHECK(r.phase == Phase::Won);
    CHECK(r.wavesCleared == 8);
    CHECK(r.hp >= 0.25f * r.hpMax);
    CHECK(r.waveSeconds >= 170.0f);
    CHECK(r.waveSeconds <= 250.0f);
    CHECK(r.fromPanels > 2.0f * (r.earned - r.fromPanels));  // sunlight is the economy
    CHECK(r.rejected == 0);
  }
}

TEST_CASE("balance: upgrades are a real alternative to extra towers, not a must-have (M2.2)") {
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    CAPTURE(seed);
    const RunResult up = play(shippedContent(), Strategy::Upgrader, seed);
    const RunResult wide = play(shippedContent(), Strategy::Sensible, seed);
    CHECK(up.phase == Phase::Won);  // fewer towers, upgraded, also hold
    CHECK(up.upgrades >= 4);
    CHECK(up.towers < wide.towers);
    CHECK(up.rejected == 0);        // the bot's preview matches the command
    CHECK(up.hp <= wide.hp);        // and do not beat building wide
  }
}

TEST_CASE("balance: the bots are deterministic") {
  for (Strategy s : kAll) {
    CHECK(play(shippedContent(), s, 7).hash == play(shippedContent(), s, 7).hash);
  }
  CHECK(play(shippedContent(), Strategy::Sensible, 7).hash != play(shippedContent(), Strategy::Sensible, 8).hash);
}

// --- M2.8 home planets -------------------------------------------------------
// Every bot (M1 and M2.9) plays every home planet over seeds 1..6. The table:
//   ./build/core_tests --no-skip -tc="planet comparison report"

namespace {

constexpr uint32_t kPlanetSeeds = 6;

struct PlanetScore {
  const PlanetDef* planet = nullptr;
  // Per strategy (index = Strategy value), over kPlanetSeeds seeds.
  uint32_t wins[kStrategyCount] = {};
  float waves[kStrategyCount] = {};     // mean waves survived
  float hpFrac[kStrategyCount] = {};    // mean HP left / HP max
  float panels[kStrategyCount] = {};    // mean stardust from panels
  float waveSecs[kStrategyCount] = {};  // mean sim seconds in waves
  uint32_t leaks[kStrategyCount] = {};  // landings over all seeds
};

// `everyBot` false plays only the four M1 bots (what the check needs, ~2 s);
// true adds the M2.9 bots for the report (~13 s).
const std::vector<PlanetScore>& planetScores(bool everyBot) {
  static std::vector<PlanetScore> cache[2];
  std::vector<PlanetScore>& scores = cache[everyBot ? 1 : 0];
  if (!scores.empty()) return scores;
  std::vector<Strategy> strategies(std::begin(kAll), std::end(kAll));
  if (everyBot) strategies = allStrategies();
  const ContentDB& db = shippedContent();
  const float n = static_cast<float>(kPlanetSeeds);
  for (size_t p = 0; p < db.planets.size(); ++p) {
    PlanetScore s;
    s.planet = &db.planets[p];
    for (Strategy strategy : strategies) {
      const int k = static_cast<int>(strategy);
      for (uint32_t seed = 1; seed <= kPlanetSeeds; ++seed) {
        const RunResult r = playOn(db, strategy, seed, static_cast<int>(p));
        s.wins[k] += r.phase == Phase::Won ? 1u : 0u;
        s.waves[k] += static_cast<float>(r.wavesCleared) / n;
        s.hpFrac[k] += r.hp / r.hpMax / n;
        s.panels[k] += r.fromPanels / n;
        s.waveSecs[k] += r.waveSeconds / n;
        s.leaks[k] += r.leaks[0] + r.leaks[1];
      }
    }
    scores.push_back(s);
  }
  return scores;
}

// What "better" means here, higher is better on every entry: sensible wins,
// HP the sensible bot keeps, its panel income, how long towers-only and the
// sun-chaser survive, and how many landings each of those two absorbs.
std::vector<float> planetMetrics(const PlanetScore& s) {
  const int towers = static_cast<int>(Strategy::Towers), chaser = static_cast<int>(Strategy::Chaser),
            sensible = static_cast<int>(Strategy::Sensible);
  return {static_cast<float>(s.wins[sensible]), s.hpFrac[sensible], s.panels[sensible], s.waves[towers],
          s.waves[chaser], -static_cast<float>(s.leaks[sensible])};
}

}  // namespace

TEST_CASE("planet comparison report" * doctest::skip()) {
  std::printf("\n| Planet | Strategy | Wins / %u | Mean waves | Mean HP left | Mean panel stardust | Wave time | Leaks (all seeds) |\n",
              kPlanetSeeds);
  std::printf("|---|---|---|---|---|---|---|---|\n");
  for (const PlanetScore& s : planetScores(true)) {
    for (Strategy strategy : allStrategies()) {
      const int k = static_cast<int>(strategy);
      std::printf("| %s | %s | %u | %.2f | %.0f%% | %.0f | %.0f s | %u |\n", s.planet->id.c_str(),
                  strategyId(strategy), s.wins[k], s.waves[k], 100.0f * s.hpFrac[k], s.panels[k], s.waveSecs[k],
                  s.leaks[k]);
    }
  }
}

TEST_CASE("balance: no home planet is strictly best") {
  const std::vector<PlanetScore>& scores = planetScores(false);
  REQUIRE(scores.size() == 3);
  for (size_t a = 0; a < scores.size(); ++a) {
    const std::vector<float> ma = planetMetrics(scores[a]);
    bool dominates = true;  // >= every other planet on every metric, > somewhere
    for (size_t b = 0; b < scores.size(); ++b) {
      if (a == b) continue;
      const std::vector<float> mb = planetMetrics(scores[b]);
      bool ge = true, gt = false;
      for (size_t i = 0; i < ma.size(); ++i) {
        ge = ge && ma[i] >= mb[i];
        gt = gt || ma[i] > mb[i];
      }
      dominates = dominates && ge && gt;
    }
    CAPTURE(scores[a].planet->id);
    CHECK_FALSE(dominates);
  }
  // Every planet stays winnable by the sensible bot and losable by never-build.
  for (const PlanetScore& s : scores) {
    CAPTURE(s.planet->id);
    CHECK(s.wins[static_cast<int>(Strategy::Sensible)] == kPlanetSeeds);
    CHECK(s.wins[static_cast<int>(Strategy::Never)] == 0);
  }
}
