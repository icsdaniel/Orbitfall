// Balance regression suite (BRIEF §8.2 targets) on the shipped content: four
// deterministic strategy bots (balance_bots.h) play whole runs. If a content
// change breaks one of these, either the change or the target is wrong; say
// which in the PR.
//
// The outcome table:  ./build/core_tests --no-skip -tc="balance report"
// (ORBITFALL_BALANCE_CONTENT=<dir> plays another content folder.)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "balance_bots.h"
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

const Strategy kAll[] = {Strategy::Never, Strategy::Towers, Strategy::Chaser, Strategy::Sensible};

}  // namespace

TEST_CASE("balance report" * doctest::skip()) {
  ContentDB other;
  const ContentDB* db = &shippedContent();
  if (const char* dir = std::getenv("ORBITFALL_BALANCE_CONTENT")) {
    std::vector<std::string> errors;
    REQUIRE(load_content(dir, other, errors));
    db = &other;
  }
  std::printf("\n| Strategy | Result | Waves survived | HP left | Stardust earned (panels) | Leaks A / B | Wave time | Towers / panels |\n");
  std::printf("|---|---|---|---|---|---|---|---|\n");
  for (Strategy s : kAll) {
    const RunResult r = play(*db, s);
    std::printf("| %s | %s | %u | %.0f / %.0f | %.0f (%.0f) | %u / %u | %.0f s | %u / %u |\n", strategyName(s),
                r.phase == Phase::Won ? "won" : (r.phase == Phase::Lost ? "lost" : "?"), r.wavesCleared,
                r.hp, r.hpMax, r.earned, r.fromPanels, r.leaks[0], r.leaks[1], r.waveSeconds, r.towers,
                r.panels);
  }
  if (std::getenv("ORBITFALL_BALANCE_TRACE")) {
    for (Strategy s : kAll) {
      std::printf("\n  %s\n", strategyName(s));
      const RunResult r = play(*db, s, 1, 99, true);
      std::printf("    leaks A/B per wave:");
      for (uint32_t wave = 0; wave < 8; ++wave) std::printf(" %u/%u", r.waveLeaks[wave][0], r.waveLeaks[wave][1]);
      std::printf("\n");
    }
  }
  std::printf("\nseeds 2..6 (waves survived, HP left):\n");
  for (Strategy s : kAll) {
    std::printf("  %-36s", strategyName(s));
    for (uint32_t seed = 2; seed <= 6; ++seed) {
      const RunResult r = play(*db, s, seed);
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

TEST_CASE("balance: the bots are deterministic") {
  for (Strategy s : kAll) {
    CHECK(play(shippedContent(), s, 7).hash == play(shippedContent(), s, 7).hash);
  }
  CHECK(play(shippedContent(), Strategy::Sensible, 7).hash != play(shippedContent(), Strategy::Sensible, 8).hash);
}
