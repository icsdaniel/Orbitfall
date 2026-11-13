#pragma once
// Deterministic strategy bots for the balance regression suite (BRIEF §8.2
// targets). Each bot plays a whole run through commands only, like a player:
// it builds in the intermissions, may rotate the planet, and starts the waves.
// Building ids are looked up at the edge; the sim only sees indices.
//
//   never      builds nothing.
//   towers     "ignores the sun": towers under both lanes, no panels, never rotates.
//   chaser     "only chases the sun": panels plus towers beside them, and it
//              keeps turning the planet so the panels sit in the beam, so its
//              towers face wherever the sun happens to be.
//   sensible   some panels, towers under both lanes, small turns for light
//              while the towers stay on their lanes.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "content/content.h"
#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/sun.h"

namespace orbitfall_bots {

using namespace orbitfall;

enum class Strategy { Never, Towers, Chaser, Sensible };

inline const char* strategyName(Strategy s) {
  switch (s) {
    case Strategy::Never: return "never-build";
    case Strategy::Towers: return "towers-only (ignores the sun)";
    case Strategy::Chaser: return "sun-chaser (towers off the lanes)";
    case Strategy::Sensible: return "sensible (panels + both lanes)";
  }
  return "?";
}

struct RunResult {
  Phase phase = Phase::Intermission;
  uint32_t wavesCleared = 0;  // waves survived
  float hp = 0.0f;
  float hpMax = 0.0f;
  float earned = 0.0f;       // stardust from panels and bounties over the run
  float fromPanels = 0.0f;   // the part of `earned` that is not bounty
  float waveSeconds = 0.0f;  // sim time spent in waves
  uint32_t kills = 0;
  uint32_t leaks[2] = {0, 0};        // landings per lane (A, B)
  float leakDamage[2] = {0.0f, 0.0f};
  uint32_t waveLeaks[16][2] = {};    // landings per wave (0-based) and lane
  uint32_t rejected = 0;
  uint32_t towers = 0, panels = 0;   // standing at the end
  uint64_t hash = 0;
};

constexpr float kDeg = kPi / 180.0f;

struct Bot {
  Strategy strategy;
  Sim& sim;
  const ContentDB& db;
  int gatling, cannon, panel;
  float budget = 0.0f;  // stardust not yet committed this intermission
  float lastInput = 0.0f;
  uint32_t towersBought = 0;

  Bot(Strategy s, Sim& simulation, const ContentDB& content)
      : strategy(s), sim(simulation), db(content),
        gatling(content.building_index("gatling")),
        cannon(content.building_index("cannon")),
        panel(content.building_index("solar_panel")) {}

  World& world() { return sim.world(); }

  bool free(int slot) { return world().slots[static_cast<size_t>(slot)].type < 0; }

  bool buy(int slot, int building) {
    const float cost = db.buildings[static_cast<size_t>(building)].cost;
    if (!free(slot) || cost > budget) return false;
    budget -= cost;
    sim.push(cmd::placeBuilding(sim.tick(), slot, building, 0.0f));
    // Mark it taken for the rest of this intermission's planning.
    planned.push_back(slot);
    return true;
  }
  std::vector<int> planned;
  bool taken(int slot) {
    if (!free(slot)) return true;
    for (int p : planned) if (p == slot) return true;
    return false;
  }

  int count(BuildingKind kind) {
    int n = 0;
    for (const BuildingSlot& b : world().slots) {
      if (b.type >= 0 && db.buildings[static_cast<size_t>(b.type)].kind == kind) ++n;
    }
    return n;
  }

  // Buys the next tower of a lane list (Gatling first, then alternating).
  bool buyTower(const std::vector<int>& slots) {
    for (int slot : slots) {
      if (taken(slot)) continue;
      // Alternate Gatling and Cannon; settle for the Gatling when short.
      const int type = (towersBought % 2 == 1) ? cannon : gatling;
      if (buy(slot, type) || (type != gatling && buy(slot, gatling))) {
        ++towersBought;
        return true;
      }
      return false;
    }
    return false;
  }

  int towersOn(const std::vector<int>& slots) {
    int n = 0;
    for (int slot : slots) if (taken(slot)) ++n;
    return n;
  }

  // Seconds the coming wave is expected to last: its spawn window plus the
  // flight to the towers.
  float expectedWaveSeconds() {
    const World& w = world();
    const WaveDef& wave = w.waveSet->waves[w.waveIndex];
    return wave.spawnSeconds + 12.0f;
  }

  // Free slot whose local angle is nearest to `local` and not reserved.
  int nearestFree(float local, const std::vector<int>& reserved, float maxOff) {
    int best = -1;
    float bestOff = maxOff;
    for (uint32_t i = 0; i < world().slotCount(); ++i) {
      bool skip = taken(static_cast<int>(i));
      for (int r : reserved) skip = skip || r == static_cast<int>(i);
      if (skip) continue;
      const float off = std::fabs(wrapPi(world().slotAngle(i) - local));
      if (off < bestOff) {
        bestOff = off;
        best = static_cast<int>(i);
      }
    }
    return best;
  }

  // --- the strategies' intermission plans ---------------------------------

  void planTowers() {
    static const std::vector<int> laneA = {3, 4, 2, 5, 1, 6}, laneB = {17, 16, 18, 15, 19, 14};
    // Lane A opens the run; afterwards keep the lanes level, A first.
    for (int guard = 0; guard < 16; ++guard) {
      const bool a = towersOn(laneA) <= towersOn(laneB);
      if (!buyTower(a ? laneA : laneB)) break;
    }
  }

  void planChaser() {
    // Panels around local angle 0, towers right beside them.
    static const std::vector<int> panels = {0, 35, 1, 34, 2, 33}, towers = {3, 32, 4, 31, 5, 30};
    for (int guard = 0; guard < 16; ++guard) {
      const int p = towersOn(panels), t = towersOn(towers);
      bool bought = false;
      // One gun first, then light before more guns.
      if (t >= 1 && (p < 2 || p <= t + 1) && p < static_cast<int>(panels.size())) {
        for (int slot : panels) {
          if (!taken(slot)) {
            bought = buy(slot, panel);
            break;
          }
        }
      } else {
        bought = buyTower(towers);
      }
      if (!bought) break;
    }
  }

  void planSensible() {
    static const std::vector<int> laneA = {3, 4, 2, 5, 1, 6}, laneB = {17, 16, 18, 15, 19, 14};
    std::vector<int> reserved = laneA;
    reserved.insert(reserved.end(), laneB.begin(), laneB.end());
    World& w = world();
    const float drift = w.sun ? w.sun->speed * expectedWaveSeconds() : 0.0f;
    const float centre = wrapTau(w.sunBearing + 0.5f * drift);  // rotation stays near 0
    const float half = 0.5f * litArc(w.planet->radius, w.sun->beamWidth);
    // Panels the sun has left behind for good are sold (70% back).
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      const BuildingSlot& b = w.slots[i];
      if (b.type != panel) continue;
      const float behind = wrapPi(w.sunBearing - w.slotAngle(i));
      if (behind > half + 20.0f * kDeg) {
        sim.push(cmd::sellBuilding(sim.tick(), static_cast<int32_t>(i)));
        budget += db.buildings[static_cast<size_t>(panel)].cost * w.planet->sellRefund;
      }
    }
    // Defence first, by what is coming on each lane; then light; then more guns.
    const uint32_t wave = w.waveIndex;
    static const int wantA[] = {1, 1, 1, 1, 2, 2, 3, 3}, wantB[] = {0, 1, 1, 1, 1, 2, 2, 3};
    static const int wantPanels[] = {1, 2, 4, 5, 5, 6, 6, 4};
    const uint32_t k = wave < 8 ? wave : 7;
    while (towersOn(laneA) < wantA[k] && buyTower(laneA)) {}
    while (towersOn(laneB) < wantB[k] && buyTower(laneB)) {}
    // A shadow caster is forecast: it will hover on the sun line, out of the
    // lane towers' arcs, so put a Gatling under the sun to shoot it down.
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      if (w.slots[i].type != gatling) continue;
      bool lane = false;
      for (int r : reserved) lane = lane || r == static_cast<int>(i);
      if (!lane && wrapPi(w.sunBearing - w.slotAngle(i)) > half) {
        sim.push(cmd::sellBuilding(sim.tick(), static_cast<int32_t>(i)));  // the sun moved on
        budget += db.buildings[static_cast<size_t>(gatling)].cost * w.planet->sellRefund;
      }
    }
    bool caster = false;
    for (const SpawnDef& sp : w.waveSet->waves[wave].spawns) {
      caster = caster || db.enemies[static_cast<size_t>(sp.enemy)].shadowCaster;
    }
    if (caster) {
      bool have = false;
      for (uint32_t i = 0; i < w.slotCount(); ++i) {
        have = have || (w.slots[i].type == gatling && std::fabs(wrapPi(w.slotAngle(i) - centre)) < 25.0f * kDeg);
      }
      if (!have) {
        const int slot = nearestFree(centre, reserved, 25.0f * kDeg);
        if (slot >= 0) buy(slot, gatling);
      }
    }
    int lit = 0;  // panels that will see the sun this wave
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      if (w.slots[i].type == panel && wrapPi(w.slotAngle(i) - w.sunBearing) > -half + drift) ++lit;
    }
    // New panels go toward the leading edge of the lit arc: lit from the first
    // second of the wave and for as long as possible after it.
    const float lead = wrapTau(w.sunBearing + half - 8.0f * kDeg);
    while (lit < wantPanels[k]) {
      const int slot = nearestFree(lead, reserved, half);
      if (slot < 0 || !buy(slot, panel)) break;
      ++lit;
    }
    for (int guard = 0; guard < 8; ++guard) {
      const bool a = towersOn(laneA) <= towersOn(laneB);
      if (!buyTower(a ? laneA : laneB) && !buyTower(a ? laneB : laneA)) break;
    }
  }

  void plan() {
    budget = world().stardust;
    planned.clear();
    switch (strategy) {
      case Strategy::Never: break;
      case Strategy::Towers: planTowers(); break;
      case Strategy::Chaser: planChaser(); break;
      case Strategy::Sensible: planSensible(); break;
    }
  }

  // --- rotation -----------------------------------------------------------

  // Planet rotation the bot wants right now; false = hands off.
  bool wantedRotation(float& out) {
    World& w = world();
    if (strategy == Strategy::Chaser) {
      // Its panels sit around local 0: keep them under the sun (a little ahead of it).
      out = wrapTau(w.sunBearing + 8.0f * kDeg);
      return true;
    }
    if (strategy == Strategy::Sensible) {
      // Mean local angle of the panels still worth lighting, as an offset from
      // the sun; turn toward it but never more than the towers' arcs forgive.
      float sx = 0.0f, sy = 0.0f;
      int n = 0;
      for (uint32_t i = 0; i < w.slotCount(); ++i) {
        if (w.slots[i].type != panel) continue;
        const float off = wrapPi(w.slotAngle(i) - w.sunBearing);
        if (std::fabs(off) > 90.0f * kDeg) continue;
        sx += std::cos(off);
        sy += std::sin(off);
        ++n;
      }
      if (n == 0) {
        out = 0.0f;
        return true;
      }
      const float off = std::atan2(sy, sx);  // panels' centre relative to the sun
      out = wrapTau(clampf(-off, -18.0f * kDeg, 18.0f * kDeg));
      return true;
    }
    return false;
  }

  // Proportional steering through RotateInput; returns the remaining error.
  float steer() {
    float target = 0.0f;
    if (!wantedRotation(target)) return 0.0f;
    World& w = world();
    const float err = wrapPi(target - w.planetRotation);
    float input = clampf(err / (25.0f * kDeg), -1.0f, 1.0f);
    if (std::fabs(err) < 0.75f * kDeg) input = 0.0f;
    if (std::fabs(input - lastInput) > 0.02f || (input == 0.0f && lastInput != 0.0f)) {
      sim.push(cmd::rotateInput(sim.tick(), input));
      lastInput = input;
    }
    return err;
  }
};

// Plays one run to the end (or `maxWaves`) and reports what happened.
inline RunResult play(const ContentDB& db, Strategy strategy, uint32_t seed = 1, uint32_t maxWaves = 99,
                      bool trace = false) {
  SimConfig cfg;
  cfg.seed = seed;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim sim(cfg);
  World& w = sim.world();
  Bot bot(strategy, sim, db);
  RunResult r;
  r.hpMax = w.planetHpMax;
  const float start = w.stardust;
  float spent = 0.0f, refunded = 0.0f, bounty = 0.0f;
  bool planned = false;
  uint32_t settle = 0;
  uint32_t guard = 0;
  while (w.phase != Phase::Won && w.phase != Phase::Lost && guard++ < 400000) {
    if (w.phase == Phase::Intermission) {
      if (w.waveIndex >= maxWaves) break;
      if (!planned) {
        bot.plan();
        planned = true;
        settle = 0;
      } else {
        // Turn into place (rotation is live between waves), then go.
        const float err = bot.steer();
        ++settle;
        if ((std::fabs(err) < 1.5f * kDeg && settle > 2) || settle > 300) {
          sim.push(cmd::startWave(sim.tick()));
          planned = false;
          if (trace) {
            int lit = 0;
            for (const BuildingSlot& b : w.slots) {
              if (b.type == bot.panel && (b.flags & kSlotLit) != 0) ++lit;
            }
            std::printf("    wave %u: hp %.0f stardust %.0f towers %d panels %d (lit %d) rot %.0f sun %.0f\n",
                        w.waveIndex + 1, w.planetHp, w.stardust, bot.count(BuildingKind::Tower),
                        bot.count(BuildingKind::Panel), lit, w.planetRotation / kDeg, w.sunBearing / kDeg);
          }
        }
      }
    } else {
      bot.steer();
      r.waveSeconds += w.dt;
    }
    sim.step();
    for (const Event& e : w.events.items) {
      switch (e.type) {
        case EventType::EnemyKilled:
          ++r.kills;
          bounty += e.value;
          break;
        case EventType::PlanetHit:
          if (e.b >= 0 && e.b < 2) {
            ++r.leaks[e.b];
            if (w.waveIndex < 16) ++r.waveLeaks[w.waveIndex][e.b];
            r.leakDamage[e.b] += e.value;
          }
          break;
        case EventType::CommandRejected: ++r.rejected; break;
        case EventType::BuildingPlaced: spent += e.value; break;
        case EventType::BuildingSold: refunded += e.value; break;
        default: break;
      }
    }
    w.events.clear();
  }
  r.phase = w.phase;
  r.wavesCleared = w.phase == Phase::Won ? w.waveCount() : w.waveIndex;
  r.hp = w.planetHp;
  r.earned = w.stardust + spent - refunded - start;
  r.fromPanels = r.earned - bounty;
  r.towers = static_cast<uint32_t>(bot.count(BuildingKind::Tower));
  r.panels = static_cast<uint32_t>(bot.count(BuildingKind::Panel));
  r.hash = sim.hash();
  return r;
}

}  // namespace orbitfall_bots
