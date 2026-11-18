// M2.10 acceptance: the three-way rotation decision on the m2_storm demo wave
// set (PLAN M2.10, DESIGN Q24). Three deterministic bots build the SAME layout
// from the SAME shopping list (a ring of Gatlings, a panel cluster around local
// 0 degrees, a shield cluster on the opposite side) and differ only in how they
// turn the planet:
//
//   sun-only     keeps the panels in the beam; ignores what is falling.
//   threat-only  keeps the shields under the threat (objects in the air, else
//                the bearing of the wave's volley); ignores the sun.
//   balance      chases the sun, but turns the shields under anything that is
//                about to come down, then goes back to the light.
//
// Expected: the sun-only bot loses to the meteor storm, the threat-only bot
// starves (a fraction of the panel income), the balancing bot does best.
// The outcome table:  ./build/core_tests --no-skip -tc="M2.10 shield bots report"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/falling.h"

using namespace orbitfall;

namespace {

constexpr float kDegR = kPi / 180.0f;

enum class Facing { SunOnly, ThreatOnly, Balance };

const char* facingName(Facing f) {
  switch (f) {
    case Facing::SunOnly: return "sun-only";
    case Facing::ThreatOnly: return "threat-only";
    case Facing::Balance: return "balance";
  }
  return "?";
}

const ContentDB& shipped() {
  static ContentDB db;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
    loaded = true;
  }
  return db;
}

struct Outcome {
  Phase phase = Phase::Intermission;
  uint32_t wavesCleared = 0;
  float hp = 0.0f;
  float fromPanels = 0.0f;  // stardust earned by panels over the run
  int absorbed = 0;
  int impacts = 0;
  float impactDamage = 0.0f;
  float landDamage = 0.0f;  // lane enemies that reached the surface
  int shields = 0, panels = 0, towers = 0;
  uint64_t hash = 0;
};

// Local layout shared by every bot (slot = 10 degrees on the medium planet).
const int kTowerSlots[] = {4, 22, 13, 31, 9, 27};
const int kPanelSlots[] = {0, 1, 35, 2, 34, 3, 33};
const int kShieldSlots[] = {18, 17, 19, 16, 20, 15, 21};
constexpr float kPanelCentre = 0.0f;              // local angle of the panel cluster
constexpr float kShieldCentre = 180.0f * kDegR;   // local angle of the shield cluster

struct ShieldBot {
  Facing facing;
  Sim& sim;
  const ContentDB& db;
  int gatling, panel, shield;
  size_t nextBuy = 0;
  float lastInput = 0.0f;
  int towers = 0, panels = 0, shields = 0;

  ShieldBot(Facing f, Sim& s, const ContentDB& content)
      : facing(f), sim(s), db(content), gatling(content.building_index("gatling")),
        panel(content.building_index("solar_panel")), shield(content.building_index("shield")) {}

  World& w() { return sim.world(); }

  // The same shopping list for every bot, bought in order while affordable.
  void plan() {
    static const char kList[] = "GPSGPSPSPGSPSPSGSP";
    float budget = w().stardust;
    while (nextBuy < sizeof(kList) - 1) {
      int type = gatling, slot = -1;
      switch (kList[nextBuy]) {
        case 'G': type = gatling; slot = towers < 6 ? kTowerSlots[towers] : -1; break;
        case 'P': type = panel; slot = panels < 7 ? kPanelSlots[panels] : -1; break;
        default: type = shield; slot = shields < 7 ? kShieldSlots[shields] : -1; break;
      }
      if (slot < 0) {
        ++nextBuy;
        continue;
      }
      const float cost = db.buildings[static_cast<size_t>(type)].cost;
      if (cost > budget) break;
      budget -= cost;
      sim.push(cmd::placeBuilding(sim.tick(), slot, type, 0.0f));
      if (type == gatling) ++towers;
      else if (type == panel) ++panels;
      else ++shields;
      ++nextBuy;
    }
  }

  // Mean world bearing of falling objects in the air that land within
  // `horizon` seconds; false when there are none.
  bool threatInAir(float horizon, float& bearing) {
    float sx = 0.0f, sy = 0.0f;
    int n = 0;
    for (uint32_t i = 0; i < w().falling.count; ++i) {
      if (fallingEta(w(), i) > horizon) continue;
      sx += std::cos(w().falling.bearing[i]);
      sy += std::sin(w().falling.bearing[i]);
      ++n;
    }
    if (n == 0) return false;
    bearing = std::atan2(sy, sx);
    return true;
  }

  // Bearing of the current (or next) wave's first volley, from the wave data
  // the forecast shows.
  bool forecastThreat(float& bearing) {
    const WaveSetDef* ws = w().waveSet;
    if (ws == nullptr || w().waveIndex >= ws->waves.size()) return false;
    const WaveDef& wave = ws->waves[w().waveIndex];
    if (wave.volleys.empty()) return false;
    bearing = wave.volleys[0].bearing;
    return true;
  }

  float wantedRotation() {
    const float sunTarget = wrapTau(w().sunBearing + 8.0f * kDegR - kPanelCentre);
    float threat = 0.0f;
    switch (facing) {
      case Facing::SunOnly:
        return sunTarget;
      case Facing::ThreatOnly:
        if (threatInAir(1.0e9f, threat) || forecastThreat(threat)) return wrapTau(threat - kShieldCentre);
        return w().planetRotation;
      case Facing::Balance:
        // Anything landing within 4 s (time to turn and catch it): shields up.
        if (threatInAir(4.0f, threat)) return wrapTau(threat - kShieldCentre);
        return sunTarget;
    }
    return 0.0f;
  }

  float steer() {
    const float err = wrapPi(wantedRotation() - w().planetRotation);
    float input = clampf(err / (25.0f * kDegR), -1.0f, 1.0f);
    if (std::fabs(err) < 0.75f * kDegR) input = 0.0f;
    if (std::fabs(input - lastInput) > 0.02f || (input == 0.0f && lastInput != 0.0f)) {
      sim.push(cmd::rotateInput(sim.tick(), input));
      lastInput = input;
    }
    return err;
  }
};

Outcome playStorm(Facing facing, uint32_t seed = 1) {
  const ContentDB& db = shipped();
  SimConfig cfg;
  cfg.seed = seed;
  cfg.content = &db;
  cfg.waveSet = db.wave_set_index("m2_storm");
  REQUIRE(cfg.waveSet != kNoIndex);
  Sim sim(cfg);
  World& w = sim.world();
  ShieldBot bot(facing, sim, db);
  Outcome out;
  float spent = 0.0f, bounty = 0.0f;
  const float start = w.stardust;
  bool planned = false;
  uint32_t settle = 0;
  for (uint32_t guard = 0; guard < 200000 && w.phase != Phase::Won && w.phase != Phase::Lost; ++guard) {
    if (w.phase == Phase::Intermission) {
      if (!planned) {
        bot.plan();
        planned = true;
        settle = 0;
      } else {
        const float err = bot.steer();
        if ((std::fabs(err) < 1.5f * kDegR && ++settle > 2) || ++settle > 300) {
          if (std::getenv("ORBITFALL_SHIELD_TRACE"))
            std::printf("  %s wave %u: hp %.0f stardust %.0f towers %d panels %d shields %d landed %.0f impacts %.0f sun %.0f\n",
                        facingName(facing), w.waveIndex + 1, w.planetHp, w.stardust, bot.towers, bot.panels,
                        bot.shields, out.landDamage, out.impactDamage, w.sunBearing / kDegR);
          sim.push(cmd::startWave(sim.tick()));
          planned = false;
        }
      }
    } else {
      bot.steer();
    }
    sim.step();
    for (const Event& e : w.events.items) {
      switch (e.type) {
        case EventType::BuildingPlaced: spent += e.value; break;
        case EventType::PlanetHit: out.landDamage += e.value; break;
        case EventType::EnemyKilled: bounty += e.value; break;
        case EventType::ShieldAbsorbed: ++out.absorbed; break;
        case EventType::FallingImpact:
          ++out.impacts;
          out.impactDamage += e.value;
          break;
        default: break;
      }
    }
    w.events.clear();
  }
  out.phase = w.phase;
  out.wavesCleared = w.phase == Phase::Won ? w.waveCount() : w.waveIndex;
  out.hp = w.planetHp;
  out.fromPanels = w.stardust + spent - start - bounty;
  out.shields = bot.shields;
  out.panels = bot.panels;
  out.towers = bot.towers;
  out.hash = sim.hash();
  return out;
}

const char* phaseName(Phase p) {
  return p == Phase::Won ? "won" : (p == Phase::Lost ? "lost" : "?");
}

}  // namespace

TEST_CASE("M2.10 shield bots report" * doctest::skip()) {
  std::printf("\n| Facing | Result | Waves | HP left | Panel income | Absorbed | Impacts (HP) | Landings (HP) | Towers / panels / shields |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|\n");
  for (Facing f : {Facing::SunOnly, Facing::ThreatOnly, Facing::Balance}) {
    const Outcome o = playStorm(f);
    std::printf("| %s | %s | %u | %.0f | %.0f | %d | %d (%.0f) | %.0f | %d / %d / %d |\n", facingName(f),
                phaseName(o.phase), o.wavesCleared, o.hp, o.fromPanels, o.absorbed, o.impacts, o.impactDamage,
                o.landDamage, o.towers, o.panels, o.shields);
  }
  std::printf("\nseeds 2..6 (waves, HP, panel income):\n");
  for (Facing f : {Facing::SunOnly, Facing::ThreatOnly, Facing::Balance}) {
    std::printf("  %-12s", facingName(f));
    for (uint32_t seed = 2; seed <= 6; ++seed) {
      const Outcome o = playStorm(f, seed);
      std::printf("  %u/%.0f/%.0f", o.wavesCleared, o.hp, o.fromPanels);
    }
    std::printf("\n");
  }
}

TEST_CASE("M2.10 three bots: facing only the sun loses to the storm, only the threat starves, balancing does best") {
  for (uint32_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const Outcome sun = playStorm(Facing::SunOnly, seed);
    const Outcome threat = playStorm(Facing::ThreatOnly, seed);
    const Outcome balance = playStorm(Facing::Balance, seed);

    // Facing only the sun: rich, but the storm gets through and wins.
    CHECK(sun.phase == Phase::Lost);
    CHECK(sun.impactDamage > threat.impactDamage);
    CHECK(sun.fromPanels > threat.fromPanels);

    // Facing only the threat: the shields catch the meteors, but the panels
    // sit in the dark: well under half the balancing bot's light income.
    CHECK(threat.fromPanels < 0.5f * balance.fromPanels);
    CHECK(threat.absorbed > sun.absorbed);

    // Balancing does best: it survives longest, with the most HP left.
    CHECK(balance.phase == Phase::Won);
    CHECK(balance.wavesCleared >= threat.wavesCleared);
    CHECK(balance.wavesCleared > sun.wavesCleared);
    CHECK(balance.hp > threat.hp);
    CHECK(balance.hp > sun.hp);
  }
}

TEST_CASE("M2.10 shield bots are deterministic") {
  for (Facing f : {Facing::SunOnly, Facing::ThreatOnly, Facing::Balance}) {
    CHECK(playStorm(f, 9).hash == playStorm(f, 9).hash);
  }
}
