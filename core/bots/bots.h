#pragma once
// Deterministic strategy bots (M1 balance suite, M2.9 sim-runner). Each bot
// plays a whole run through commands only, like a player: it builds in the
// intermissions, may rotate the planet, and starts the waves. Building ids are
// looked up at the edge; the sim only sees indices.
//
// One implementation, two users: the doctest balance suite
// (core/tests/test_balance.cpp, test_threats.cpp) and the headless
// tools/sim-runner. Not part of the sim: nothing under core/sim or
// core/systems may include this, and the GDExtension does not build it.
//
// M1 bots (their outcomes are pinned by test_balance.cpp):
//   never-build           builds nothing (control, never counted as an answer).
//   towers-only           "ignores the sun": towers under both lanes, no panels, never rotates.
//   sun-chaser            "only chases the sun": panels plus towers beside them; keeps turning
//                         the planet so the panels sit in the beam.
//   sensible              some panels, towers under the lanes, small turns for light while the
//                         towers stay on their lanes.
// M2.9 bots:
//   never-rotate          the sensible build (lane towers, panels where the sun is), hands off
//                         the rotation for the whole run.
//   rotate-to-lane        one tower battery; turns it to the lane carrying the most threat.
//   rotate-to-weak-point  same battery; turns it to the nearest enemy (stub until M2.4 adds
//                         weak points).
//   chase-the-light       a fixed panel field kept under the sun all run; guns are added
//                         wherever the lanes happen to be.
//   face-the-threat       same battery; turns to whatever rotation covers the most incoming
//                         landing damage, never looks at the sun.
//   balance               lane towers and panels; every moment weighs lit panels against
//                         covered threat and turns to the better rotation.
//   bank-and-drill        the sensible build, but banks stardust ahead of dim waves and buys
//                         drills instead of panels (the drill part is a no-op until M2.7).
// M2.2 bot:
//   upgrader              sensible, but the stardust left after the wanted towers and panels
//                         buys upgrade tiers (cheapest first, one path per tower) instead of
//                         extra towers.
#include <cstdint>
#include <string>
#include <vector>

#include "content/content.h"
#include "sim/world.h"

namespace orbitfall_bots {

using namespace orbitfall;

enum class Strategy {
  Never,
  Towers,
  Chaser,
  Sensible,
  NeverRotate,
  RotateToLane,
  RotateToWeakPoint,
  ChaseTheLight,
  FaceTheThreat,
  Balance,
  BankAndDrill,
  Upgrader,  // M2.2
  Count
};

constexpr int kStrategyCount = static_cast<int>(Strategy::Count);

// Every strategy, in enum order.
const std::vector<Strategy>& allStrategies();
// Long label for tables ("sensible (panels + both lanes)").
const char* strategyName(Strategy s);
// Short stable id for the CLI and the CSV ("sensible", "rotate-to-lane").
const char* strategyId(Strategy s);
// Answer family (PLAN §5.1b): control, tower, light, layout, rotation, mixed, economy.
const char* strategyFamily(Strategy s);
// False for the control (never-build): it is not an answer to anything.
bool strategyIsAnswer(Strategy s);
// Strategy with this id; false when unknown.
bool strategyFromId(const std::string& id, Strategy& out);

constexpr uint32_t kMaxLanesTracked = 2;
constexpr uint32_t kMaxWavesTracked = 16;

struct WaveResult {
  bool started = false;
  bool cleared = false;
  float hpLost = 0.0f;
  float seconds = 0.0f;  // sim time spent in the wave
  uint32_t leaks = 0;
};

struct RunResult {
  Phase phase = Phase::Intermission;
  uint32_t wavesCleared = 0;  // waves survived
  uint32_t wavesTotal = 0;
  float hp = 0.0f;
  float hpMax = 0.0f;
  float earned = 0.0f;       // stardust from panels and bounties over the run
  float fromPanels = 0.0f;   // the part of `earned` that is not bounty
  float spent = 0.0f;        // stardust paid for buildings
  float stardust = 0.0f;     // left in the bank at the end
  float waveSeconds = 0.0f;  // sim time spent in waves
  uint32_t ticks = 0;        // sim ticks played, intermissions included
  uint32_t kills = 0;
  uint32_t leaks[kMaxLanesTracked] = {0, 0};  // landings per lane (A, B)
  float leakDamage[kMaxLanesTracked] = {0.0f, 0.0f};
  uint32_t waveLeaks[kMaxWavesTracked][kMaxLanesTracked] = {};  // landings per wave (0-based) and lane
  uint32_t rejected = 0;
  uint32_t towers = 0, panels = 0;  // standing at the end
  uint32_t upgrades = 0;            // upgrade tiers bought (M2.2)
  uint64_t hash = 0;
  // Per enemy definition (index into ContentDB::enemies), all lanes.
  std::vector<uint32_t> leaksByEnemy;
  std::vector<float> leakDamageByEnemy;
  std::vector<uint32_t> killsByEnemy;
  // Per wave of the wave set (0-based).
  std::vector<WaveResult> waves;

  uint32_t totalLeaks() const {
    uint32_t n = 0;
    for (uint32_t v : leaksByEnemy) n += v;
    return n;
  }
};

struct RunOptions {
  uint32_t seed = 1;
  int waveSet = 0;         // index into ContentDB::waveSets
  uint32_t maxWaves = 99;  // stop before this 0-based wave
  bool trace = false;      // print the state at every wave start
  int planet = kNoIndex;   // M2.8: home planet (index into ContentDB::planets), kNoIndex = the wave set's
};

// M2.4: rotate input in [-1, 1] that turns the planet so a precision tower
// (the Lance) sits right under the nearest weak-point enemy (weakPointHint),
// braking in time to stop on it; 0 when there is nothing to line up. For
// scripted scenarios that drive the rotation themselves.
float weakPointRotateInput(const World& world);

// Plays one run to the end (or `maxWaves`) and reports what happened.
RunResult play(const ContentDB& db, Strategy strategy, const RunOptions& options);
RunResult play(const ContentDB& db, Strategy strategy, uint32_t seed = 1, uint32_t maxWaves = 99,
               bool trace = false);

// --- threat coverage (PLAN §5.1b: every threat has at least three answers) ---

struct ThreatOptions {
  int waveSet = 0;
  uint32_t firstSeed = 1;
  uint32_t seeds = 5;
  // A strategy beats a wave in one run when it clears the wave having lost at
  // most this fraction of the planet's max HP during it...
  float maxHpLossFrac = 0.25f;
  // ...and it beats the wave overall when it does so in more than this
  // fraction of the seeds.
  float minSeedFrac = 0.5f;
  uint32_t needed = 3;  // strategies that must beat every wave
};

struct WaveCoverage {
  int wave = 0;  // 1-based number from the wave set
  std::vector<Strategy> beatenBy;
  uint32_t families = 0;  // distinct answer families among beatenBy
  bool ok = false;        // beatenBy.size() >= needed
};

struct ThreatReport {
  std::vector<WaveCoverage> waves;
  uint32_t failing = 0;  // waves with fewer than `needed` answers
};

// Folds finished runs into a report. `runs[i]` was played by `strategies[i]`;
// several seeds per strategy are expected. Control strategies are ignored.
ThreatReport threatCoverage(const ContentDB& db, int waveSet, const std::vector<Strategy>& strategies,
                            const std::vector<RunResult>& runs, const ThreatOptions& options);
// Plays `options.seeds` runs per strategy and folds them.
ThreatReport threatCoverage(const ContentDB& db, const std::vector<Strategy>& strategies,
                            const ThreatOptions& options);
// Markdown table of a report, one row per wave.
std::string formatThreatReport(const ThreatReport& report, const ThreatOptions& options);

}  // namespace orbitfall_bots
