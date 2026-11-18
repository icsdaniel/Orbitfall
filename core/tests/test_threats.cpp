// M2.9: the strategy bots beyond the M1 four (core/bots/bots.h), and the
// "multiple answers per threat" check of PLAN §5.1b: every wave should be
// beaten by at least three different strategies.
//
// The check is a report, not a gate: it is opt-in and it WARNs about the waves
// that fail it instead of failing CI on the current content.
//
//   ./build/core_tests --no-skip -tc="threat coverage report"
//   (ORBITFALL_BALANCE_CONTENT=<dir> checks another content folder,
//    ORBITFALL_THREAT_SEEDS=<n> changes the seeds per strategy, default 5.)
#include <cstdio>
#include <cstdlib>
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

}  // namespace

TEST_CASE("threat coverage report" * doctest::skip()) {
  ContentDB other;
  const ContentDB* db = &shippedContent();
  if (const char* dir = std::getenv("ORBITFALL_BALANCE_CONTENT")) {
    std::vector<std::string> errors;
    REQUIRE(load_content(dir, other, errors));
    db = &other;
  }
  ThreatOptions options;
  if (const char* seeds = std::getenv("ORBITFALL_THREAT_SEEDS")) {
    options.seeds = static_cast<uint32_t>(std::strtoul(seeds, nullptr, 10));
  }
  for (size_t set = 0; set < db->waveSets.size(); ++set) {
    options.waveSet = static_cast<int>(set);
    const ThreatReport report = threatCoverage(*db, allStrategies(), options);
    std::printf("\nwave set %s, %u seeds per strategy:\n\n%s", db->waveSets[set].id.c_str(), options.seeds,
                formatThreatReport(report, options).c_str());
    for (const WaveCoverage& c : report.waves) {
      CAPTURE(db->waveSets[set].id);
      CAPTURE(c.wave);
      CAPTURE(c.beatenBy.size());
      WARN_MESSAGE(c.ok, "wave has fewer than three answers");
    }
  }
}

TEST_CASE("bots: every strategy has a unique id that round-trips") {
  CHECK(allStrategies().size() == static_cast<size_t>(kStrategyCount));
  for (Strategy s : allStrategies()) {
    Strategy back = Strategy::Count;
    CHECK(strategyFromId(strategyId(s), back));
    CHECK(back == s);
    for (Strategy t : allStrategies()) {
      if (s != t) CHECK(std::string(strategyId(s)) != strategyId(t));
    }
  }
  Strategy none;
  CHECK_FALSE(strategyFromId("no-such-bot", none));
  CHECK_FALSE(strategyIsAnswer(Strategy::Never));
}

TEST_CASE("bots: every strategy plays a legal, deterministic run") {
  for (Strategy s : allStrategies()) {
    CAPTURE(strategyId(s));
    const RunResult a = play(shippedContent(), s, 3);
    const RunResult b = play(shippedContent(), s, 3);
    CHECK(a.hash == b.hash);
    CHECK((a.phase == Phase::Won || a.phase == Phase::Lost));
    CHECK(a.rejected == 0);  // the bots only send commands the sim accepts
    CHECK(a.wavesTotal == 8);
    // The per-enemy and per-wave tallies agree with the totals.
    CHECK(a.totalLeaks() == a.leaks[0] + a.leaks[1]);
    uint32_t kills = 0, waveLeaks = 0, cleared = 0;
    float hpLost = 0.0f;
    for (uint32_t k : a.killsByEnemy) kills += k;
    for (const WaveResult& w : a.waves) {
      waveLeaks += w.leaks;
      hpLost += w.hpLost;
      cleared += w.cleared ? 1u : 0u;
    }
    CHECK(kills == a.kills);
    CHECK(waveLeaks == a.totalLeaks());
    CHECK(cleared == a.wavesCleared);
    if (a.phase == Phase::Won) CHECK(hpLost == doctest::Approx(a.hpMax - a.hp).epsilon(0.001));
  }
}

TEST_CASE("bots: the M2.9 strategies behave as named") {
  const ContentDB& db = shippedContent();
  // never-rotate really keeps its hands off: same build as the sensible bot,
  // so it holds the early waves, and it earns from panels.
  const RunResult still = play(db, Strategy::NeverRotate);
  CHECK(still.wavesCleared >= 4);
  CHECK(still.fromPanels > 100.0f);
  // chase-the-light keeps its field lit: sunlight is most of its income.
  const RunResult light = play(db, Strategy::ChaseTheLight);
  CHECK(light.fromPanels > 2.0f * (light.earned - light.fromPanels));
  // The battery bots shoot things down: aiming works.
  for (Strategy s : {Strategy::RotateToLane, Strategy::RotateToWeakPoint, Strategy::FaceTheThreat}) {
    CAPTURE(strategyId(s));
    const RunResult r = play(db, s);
    CHECK(r.kills > 50);
    CHECK(r.wavesCleared >= 3);
  }
  // Every answer strategy outlasts building nothing.
  const RunResult never = play(db, Strategy::Never);
  for (Strategy s : allStrategies()) {
    if (!strategyIsAnswer(s)) continue;
    CAPTURE(strategyId(s));
    CHECK(play(db, s).wavesCleared > never.wavesCleared);
  }
}

TEST_CASE("bots: threat coverage counts strategies that clear a wave cheaply") {
  const ContentDB& db = shippedContent();
  // Hand-made results on the shipped wave set: A clears everything unhurt, B
  // loses 30 HP in wave 2, the control clears everything too.
  auto run = [&](float wave2Loss, uint32_t cleared) {
    RunResult r;
    r.hpMax = 100.0f;
    r.waves.assign(db.waveSets[0].waves.size(), WaveResult());
    for (uint32_t w = 0; w < cleared && w < r.waves.size(); ++w) {
      r.waves[w].started = r.waves[w].cleared = true;
    }
    r.waves[1].hpLost = wave2Loss;
    return r;
  };
  const std::vector<Strategy> who = {Strategy::Sensible, Strategy::Towers, Strategy::Never, Strategy::Balance};
  const std::vector<RunResult> runs = {run(0.0f, 8), run(30.0f, 8), run(0.0f, 8), run(0.0f, 3)};
  ThreatOptions options;
  const ThreatReport report = threatCoverage(db, 0, who, runs, options);
  REQUIRE(report.waves.size() == 8);
  CHECK(report.waves[0].beatenBy.size() == 3);  // sensible, towers, balance; never-build is a control
  CHECK(report.waves[0].ok);
  CHECK(report.waves[0].families == 2);         // mixed and tower
  CHECK(report.waves[1].beatenBy.size() == 2);  // towers lost too much HP
  CHECK_FALSE(report.waves[1].ok);
  CHECK(report.waves[3].beatenBy.size() == 2);  // balance stopped after wave 3
  CHECK(report.waves[2].ok);
  CHECK(report.failing == 6);
  CHECK(formatThreatReport(report, options).find("TOO FEW") != std::string::npos);
}
