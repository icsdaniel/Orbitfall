// sim-runner (M2.9): plays N seeded runs per strategy bot on a wave set,
// headless and without Godot, and writes one CSV row per run. See README.md.
//
// The bots are core/bots (shared with the doctest balance suite). The sim is
// deterministic, so the CSV is too: the wall-clock timing only goes to stderr.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bots/bots.h"
#include "content/content_loader.h"

using namespace orbitfall;
using namespace orbitfall_bots;

namespace {

struct Args {
  std::string content = ORBITFALL_CONTENT_DIR;
  std::string waves;  // wave set id; empty = the first one
  std::string out = "sim-runner.csv";
  std::string bots = "all";
  uint32_t runs = 100;  // seeds per bot
  uint32_t seed = 1;    // first seed
  uint32_t maxWaves = 99;
  ThreatOptions threat;
  bool quiet = false;
  bool strict = false;  // exit 2 when a wave has too few answers
};

void usage() {
  std::printf(
      "sim-runner: seeded headless bot runs, results as CSV\n\n"
      "  --content DIR     content folder (default: the repo's content/)\n"
      "  --waves ID        wave set id (default: the first wave set); it names the planet and sun\n"
      "  --runs N          seeds per bot (default 100)\n"
      "  --seed S          first seed (default 1); seeds are S .. S+N-1\n"
      "  --bots a,b,c      bot ids, or 'all' (default); see --list-bots\n"
      "  --max-waves N     stop each run before 0-based wave N\n"
      "  --out FILE        CSV path (default sim-runner.csv); '-' = stdout\n"
      "  --beaten-hp F     a wave is beaten when cleared losing <= F of max HP (default 0.25)\n"
      "  --beaten-seeds F  ...in more than F of the seeds (default 0.5)\n"
      "  --answers N       strategies that must beat every wave (default 3)\n"
      "  --strict          exit with status 2 when a wave has too few answers\n"
      "  --quiet           no summary tables\n"
      "  --list-bots       print the bot ids and exit\n");
}

bool parse(int argc, char** argv, Args& a, bool& exitNow) {
  exitNow = false;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto value = [&](std::string& out) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "sim-runner: %s needs a value\n", k.c_str());
        return false;
      }
      out = argv[++i];
      return true;
    };
    std::string v;
    if (k == "--help" || k == "-h") {
      usage();
      exitNow = true;
    } else if (k == "--list-bots") {
      for (Strategy s : allStrategies()) {
        std::printf("%-22s %-9s %s\n", strategyId(s), strategyFamily(s), strategyName(s));
      }
      exitNow = true;
    } else if (k == "--quiet") {
      a.quiet = true;
    } else if (k == "--strict") {
      a.strict = true;
    } else if (k == "--content") {
      if (!value(a.content)) return false;
    } else if (k == "--waves") {
      if (!value(a.waves)) return false;
    } else if (k == "--out") {
      if (!value(a.out)) return false;
    } else if (k == "--bots") {
      if (!value(a.bots)) return false;
    } else if (k == "--runs") {
      if (!value(v)) return false;
      a.runs = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else if (k == "--seed") {
      if (!value(v)) return false;
      a.seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else if (k == "--max-waves") {
      if (!value(v)) return false;
      a.maxWaves = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else if (k == "--beaten-hp") {
      if (!value(v)) return false;
      a.threat.maxHpLossFrac = std::strtof(v.c_str(), nullptr);
    } else if (k == "--beaten-seeds") {
      if (!value(v)) return false;
      a.threat.minSeedFrac = std::strtof(v.c_str(), nullptr);
    } else if (k == "--answers") {
      if (!value(v)) return false;
      a.threat.needed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else {
      std::fprintf(stderr, "sim-runner: unknown option %s (try --help)\n", k.c_str());
      return false;
    }
  }
  return true;
}

bool pickBots(const std::string& list, std::vector<Strategy>& out) {
  if (list == "all") {
    out = allStrategies();
    return true;
  }
  size_t from = 0;
  while (from <= list.size()) {
    size_t to = list.find(',', from);
    if (to == std::string::npos) to = list.size();
    const std::string id = list.substr(from, to - from);
    Strategy s;
    if (!strategyFromId(id, s)) {
      std::fprintf(stderr, "sim-runner: unknown bot '%s' (see --list-bots)\n", id.c_str());
      return false;
    }
    out.push_back(s);
    from = to + 1;
  }
  return !out.empty();
}

const char* outcome(const RunResult& r) {
  return r.phase == Phase::Won ? "won" : (r.phase == Phase::Lost ? "lost" : "unfinished");
}

void writeCsv(std::FILE* f, const ContentDB& db, const WaveSetDef& set, const std::vector<Strategy>& who,
              const std::vector<uint32_t>& seeds, const std::vector<RunResult>& runs) {
  std::fprintf(f, "wave_set,planet,seed,bot,family,outcome,waves_survived,waves_total,hp_left,hp_max,"
                  "stardust_earned,stardust_from_panels,stardust_spent,stardust_left,run_seconds,ticks,"
                  "kills,leaks,leak_damage,towers,panels,rejected,hash");
  for (const EnemyDef& e : db.enemies) std::fprintf(f, ",leaks_%s", e.id.c_str());
  for (const WaveDef& w : set.waves) std::fprintf(f, ",wave%d_hp_lost", w.wave);
  std::fprintf(f, "\n");
  const std::string& planet = db.planets[static_cast<size_t>(set.planet)].id;
  for (size_t i = 0; i < runs.size(); ++i) {
    const RunResult& r = runs[i];
    float damage = 0.0f;
    for (float d : r.leakDamageByEnemy) damage += d;
    std::fprintf(f, "%s,%s,%u,%s,%s,%s,%u,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%u,%u,%u,%.1f,%u,%u,%u,%016llx",
                 set.id.c_str(), planet.c_str(), seeds[i], strategyId(who[i]), strategyFamily(who[i]), outcome(r),
                 r.wavesCleared, r.wavesTotal, r.hp, r.hpMax, r.earned, r.fromPanels, r.spent, r.stardust,
                 r.waveSeconds, r.ticks, r.kills, r.totalLeaks(), damage, r.towers, r.panels, r.rejected,
                 static_cast<unsigned long long>(r.hash));
    for (uint32_t n : r.leaksByEnemy) std::fprintf(f, ",%u", n);
    // Empty = the run never reached that wave.
    for (const WaveResult& w : r.waves) {
      if (w.started) std::fprintf(f, ",%.1f", w.hpLost);
      else std::fprintf(f, ",");
    }
    std::fprintf(f, "\n");
  }
}

void printSummary(const ContentDB& db, const std::vector<Strategy>& bots, const std::vector<Strategy>& who,
                  const std::vector<RunResult>& runs) {
  std::printf("\n| Bot | Family | Runs | Won | Waves survived (min / mean / max) | HP left (mean) | "
              "Stardust earned (mean) | Run seconds (mean) | Leaks (mean) | Most leaked |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|---|\n");
  for (Strategy s : bots) {
    uint32_t n = 0, won = 0, minW = 0, maxW = 0;
    double waves = 0.0, hp = 0.0, earned = 0.0, seconds = 0.0, leaks = 0.0;
    std::vector<uint64_t> byEnemy(db.enemies.size(), 0);
    for (size_t i = 0; i < runs.size(); ++i) {
      if (who[i] != s) continue;
      const RunResult& r = runs[i];
      if (n == 0 || r.wavesCleared < minW) minW = r.wavesCleared;
      if (n == 0 || r.wavesCleared > maxW) maxW = r.wavesCleared;
      ++n;
      won += r.phase == Phase::Won ? 1u : 0u;
      waves += r.wavesCleared;
      hp += r.hp;
      earned += r.earned;
      seconds += r.waveSeconds;
      leaks += r.totalLeaks();
      for (size_t e = 0; e < byEnemy.size(); ++e) byEnemy[e] += r.leaksByEnemy[e];
    }
    if (n == 0) continue;
    size_t worst = 0;
    for (size_t e = 1; e < byEnemy.size(); ++e) {
      if (byEnemy[e] > byEnemy[worst]) worst = e;
    }
    const bool any = !byEnemy.empty() && byEnemy[worst] > 0;
    const double d = static_cast<double>(n);
    std::printf("| %s | %s | %u | %u | %u / %.1f / %u | %.0f | %.0f | %.0f | %.1f | %s |\n", strategyId(s),
                strategyFamily(s), n, won, minW, waves / d, maxW, hp / d, earned / d, seconds / d, leaks / d,
                any ? db.enemies[worst].id.c_str() : "-");
  }
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  bool exitNow = false;
  if (!parse(argc, argv, args, exitNow)) return 1;
  if (exitNow) return 0;

  ContentDB db;
  std::vector<std::string> errors;
  if (!load_content(args.content, db, errors)) {
    std::fprintf(stderr, "sim-runner: cannot load content from %s\n", args.content.c_str());
    for (const std::string& e : errors) std::fprintf(stderr, "  %s\n", e.c_str());
    return 1;
  }
  int waveSet = 0;
  if (!args.waves.empty()) waveSet = db.wave_set_index(args.waves);
  if (waveSet < 0 || static_cast<size_t>(waveSet) >= db.waveSets.size()) {
    std::fprintf(stderr, "sim-runner: unknown wave set '%s'; known:", args.waves.c_str());
    for (const WaveSetDef& w : db.waveSets) std::fprintf(stderr, " %s", w.id.c_str());
    std::fprintf(stderr, "\n");
    return 1;
  }
  const WaveSetDef& set = db.waveSets[static_cast<size_t>(waveSet)];
  std::vector<Strategy> bots;
  if (!pickBots(args.bots, bots)) return 1;
  if (args.runs == 0) {
    std::fprintf(stderr, "sim-runner: --runs must be at least 1\n");
    return 1;
  }

  std::vector<Strategy> who;
  std::vector<uint32_t> seeds;
  std::vector<RunResult> runs;
  who.reserve(bots.size() * args.runs);
  seeds.reserve(bots.size() * args.runs);
  runs.reserve(bots.size() * args.runs);
  const auto t0 = std::chrono::steady_clock::now();
  for (Strategy s : bots) {
    for (uint32_t i = 0; i < args.runs; ++i) {
      RunOptions run;
      run.seed = args.seed + i;
      run.waveSet = waveSet;
      run.maxWaves = args.maxWaves;
      who.push_back(s);
      seeds.push_back(run.seed);
      runs.push_back(play(db, s, run));
    }
  }
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::FILE* f = args.out == "-" ? stdout : std::fopen(args.out.c_str(), "w");
  if (!f) {
    std::fprintf(stderr, "sim-runner: cannot write %s\n", args.out.c_str());
    return 1;
  }
  writeCsv(f, db, set, who, seeds, runs);
  if (f != stdout) std::fclose(f);

  double simSeconds = 0.0;
  for (const RunResult& r : runs) simSeconds += r.waveSeconds;
  std::fprintf(stderr, "sim-runner: %zu runs (%zu bots x %u seeds, wave set %s) in %.2f s wall, %.0f s of wave time simulated; CSV: %s\n",
               runs.size(), bots.size(), args.runs, set.id.c_str(), wall, simSeconds, args.out.c_str());

  args.threat.waveSet = waveSet;
  args.threat.firstSeed = args.seed;
  args.threat.seeds = args.runs;
  const ThreatReport report = threatCoverage(db, waveSet, who, runs, args.threat);
  if (!args.quiet && args.out != "-") {
    printSummary(db, bots, who, runs);
    std::printf("\nThreat coverage (a wave is beaten when cleared losing <= %.0f%% of max HP, in > %.0f%% of seeds):\n\n%s",
                100.0f * args.threat.maxHpLossFrac, 100.0f * args.threat.minSeedFrac,
                formatThreatReport(report, args.threat).c_str());
  }
  return args.strict && report.failing > 0 ? 2 : 0;
}
