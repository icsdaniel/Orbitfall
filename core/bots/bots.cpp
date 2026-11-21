// Strategy bots: see bots.h. The M1 bots (never, towers, chaser, sensible) are
// pinned by core/tests/test_balance.cpp; change them only together with the
// balance targets.
#include "bots/bots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "content/content.h"
#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "systems/sun.h"
#include "systems/upgrades.h"

namespace orbitfall_bots {

namespace {

struct StrategyInfo {
  Strategy strategy;
  const char* id;
  const char* name;
  const char* family;
  bool answer;
};

const StrategyInfo kInfo[kStrategyCount] = {
    {Strategy::Never, "never-build", "never-build", "control", false},
    {Strategy::Towers, "towers-only", "towers-only (ignores the sun)", "tower", true},
    {Strategy::Chaser, "sun-chaser", "sun-chaser (towers off the lanes)", "light", true},
    {Strategy::Sensible, "sensible", "sensible (panels + both lanes)", "mixed", true},
    {Strategy::NeverRotate, "never-rotate", "never-rotate (sensible build, hands off)", "layout", true},
    {Strategy::RotateToLane, "rotate-to-lane", "rotate-to-lane (battery to the busiest lane)", "rotation", true},
    {Strategy::RotateToWeakPoint, "rotate-to-weak-point", "rotate-to-weak-point (Lance under the weak point)",
     "rotation", true},
    {Strategy::ChaseTheLight, "chase-the-light", "chase-the-light (panel field under the sun)", "light", true},
    {Strategy::FaceTheThreat, "face-the-threat", "face-the-threat (battery to the most damage)", "rotation", true},
    {Strategy::Balance, "balance", "balance (weighs light against threat)", "mixed", true},
    {Strategy::BankAndDrill, "bank-and-drill", "bank-and-drill (banks before dim waves)", "economy", true},
    {Strategy::Upgrader, "upgrader", "upgrader (sensible, upgrades not extras)", "mixed", true},
};

const StrategyInfo& info(Strategy s) {
  const int i = static_cast<int>(s);
  return kInfo[i >= 0 && i < kStrategyCount ? i : 0];
}

}  // namespace

const std::vector<Strategy>& allStrategies() {
  static const std::vector<Strategy> all = [] {
    std::vector<Strategy> v;
    for (const StrategyInfo& i : kInfo) v.push_back(i.strategy);
    return v;
  }();
  return all;
}
const char* strategyName(Strategy s) { return info(s).name; }
const char* strategyId(Strategy s) { return info(s).id; }
const char* strategyFamily(Strategy s) { return info(s).family; }
bool strategyIsAnswer(Strategy s) { return info(s).answer; }
bool strategyFromId(const std::string& id, Strategy& out) {
  for (const StrategyInfo& i : kInfo) {
    if (id == i.id) {
      out = i.strategy;
      return true;
    }
  }
  return false;
}

namespace {

constexpr float kDeg = kPi / 180.0f;

// Build tables shared by the M2.9 bots, by 0-based wave (clamped to the last
// entry): towers wanted on the first lane and on every other lane, and panels
// wanted in the light. Same numbers as the sensible bot.
const int kWantFirstLane[] = {1, 1, 1, 1, 2, 2, 3, 3};
const int kWantOtherLane[] = {0, 1, 1, 1, 1, 2, 2, 3};
const int kWantPanels[] = {1, 2, 4, 5, 5, 6, 6, 4};
const int kWantFieldPanels[] = {1, 2, 4, 5, 6, 7, 8, 8};  // chase-the-light never sells

// Threat grid of the scored bots: enemies binned by bearing and height.
constexpr int kBearingBins = 72;
constexpr int kHeightBins = 14;
constexpr float kHeightStep = 45.0f;

struct Bot {
  Strategy strategy;
  Sim& sim;
  const ContentDB& db;
  int gatling, cannon, panel;
  int drill;  // kNoIndex until M2.7 ships one
  int precision;  // M2.4: the first precision tower (the Lance), by data flag
  float budget = 0.0f;  // stardust not yet committed this intermission
  float lastInput = 0.0f;
  uint32_t towersBought = 0;

  Bot(Strategy s, Sim& simulation, const ContentDB& content)
      : strategy(s), sim(simulation), db(content),
        gatling(content.building_index("gatling")),
        cannon(content.building_index("cannon")),
        panel(content.building_index("solar_panel")),
        drill(content.building_index("drill")),
        precision(firstPrecisionTower(content)) {
    m1Layout();
  }

  // --- M1 bots' slot layout (M2.8: derived from the planet so every home
  // planet plays the same plan; on the 36-slot medium planet these are the M1
  // lists {3, 4, 2, 5, 1, 6}, {17, 16, 18, 15, 19, 14}, ...) ---
  std::vector<int> laneA, laneB;  // towers under each lane, best first
  std::vector<int> chaserPanels, chaserTowers;

  int slotAt(int i) {
    const int n = static_cast<int>(world().slotCount());
    return n > 0 ? ((i % n) + n) % n : 0;
  }
  // Slots a tower can cover a lane from: within 35 degrees of it, nearest
  // first, ties stepping toward the other lane; at most 6.
  std::vector<int> m1LaneSlots(float bearing, float other) {
    const World& w = world();
    const int n = static_cast<int>(w.slotCount());
    const int dir = wrapPi(other - bearing) >= 0.0f ? 1 : -1;
    std::vector<std::pair<float, int>> near;  // (sort key, slot)
    for (int i = 0; i < n; ++i) {
      const float off = wrapPi(w.slotAngle(static_cast<uint32_t>(i)) - bearing);
      const float dist = std::round(std::fabs(off) / kDeg * 100.0f) / 100.0f;
      if (dist > 35.0f) continue;
      const bool toward = (off > 0.0f ? 1 : -1) == dir;
      near.push_back({dist * 2.0f + (toward || dist == 0.0f ? 0.0f : 1.0f), i});
    }
    std::stable_sort(near.begin(), near.end(),
                     [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first < y.first; });
    std::vector<int> out;
    for (size_t k = 0; k < near.size() && k < 6; ++k) out.push_back(near[k].second);
    return out;
  }
  void m1Layout() {
    const World& w = world();
    if (w.planet == nullptr || w.planet->lanes.size() < 2 || w.slotCount() == 0) return;
    const float a = w.planet->lanes[0].bearing, b = w.planet->lanes[1].bearing;
    laneA = m1LaneSlots(a, b);
    laneB = m1LaneSlots(b, a);
    static const int panels[] = {0, -1, 1, -2, 2, -3}, towers[] = {3, -4, 4, -5, 5, -6};
    for (int q : panels) chaserPanels.push_back(slotAt(q));
    for (int t : towers) chaserTowers.push_back(slotAt(t));
  }

  static int firstPrecisionTower(const ContentDB& content) {
    for (size_t i = 0; i < content.buildings.size(); ++i)
      if (content.buildings[i].precision) return static_cast<int>(i);
    return kNoIndex;
  }

  World& world() { return sim.world(); }

  bool free(int slot) {
    // Slots a small planet does not have are never free.
    if (slot < 0 || slot >= static_cast<int>(world().slotCount())) return false;
    return world().slots[static_cast<size_t>(slot)].type < 0;
  }

  bool buy(int slot, int building) {
    if (building < 0) return false;
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

  // --- the M1 strategies' intermission plans -------------------------------

  void planTowers() {
    // Lane A opens the run; afterwards keep the lanes level, A first.
    for (int guard = 0; guard < 16; ++guard) {
      const bool a = towersOn(laneA) <= towersOn(laneB);
      if (!buyTower(a ? laneA : laneB)) break;
    }
  }

  void planChaser() {
    // Panels around local angle 0, towers right beside them.
    const std::vector<int>& panels = chaserPanels;
    const std::vector<int>& towers = chaserTowers;
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
    if (strategy == Strategy::Upgrader) {
      buyUpgrades();
      return;
    }
    for (int guard = 0; guard < 8; ++guard) {
      const bool a = towersOn(laneA) <= towersOn(laneB);
      if (!buyTower(a ? laneA : laneB) && !buyTower(a ? laneB : laneA)) break;
    }
  }

  // M2.2: spends the budget on the cheapest next tier of a standing tower
  // (either ring), committing each tower to path 0 (path 1 then stops at the
  // exclusive tier).
  void buyUpgrades() {
    World& w = world();
    const uint32_t sites = w.siteCount();
    std::vector<std::array<uint8_t, kUpgradePaths>> tiers(sites);
    for (uint32_t i = 0; i < sites; ++i) {
      for (int p = 0; p < kUpgradePaths; ++p) tiers[i][static_cast<size_t>(p)] = w.site(i).tiers[p];
    }
    for (int guard = 0; guard < 64; ++guard) {
      int bestSite = -1, bestPath = 0;
      float bestCost = 0.0f;
      for (uint32_t i = 0; i < sites; ++i) {
        const int32_t type = w.site(i).type;
        if (type < 0) continue;
        const BuildingDef& def = db.buildings[static_cast<size_t>(type)];
        if (def.kind != BuildingKind::Tower) continue;
        for (int p = 0; p < kUpgradePaths; ++p) {
          const UpgradePreview pv = upgradePreview(db.rules, def, tiers[i].data(), p, budget);
          if (pv.reason != RejectReason::None) continue;
          if (bestSite < 0 || pv.cost < bestCost) {
            bestSite = static_cast<int>(i);
            bestPath = p;
            bestCost = pv.cost;
          }
          break;  // path 0 first; path 1 only once path 0 is done
        }
      }
      if (bestSite < 0) return;
      budget -= bestCost;
      ++tiers[static_cast<size_t>(bestSite)][static_cast<size_t>(bestPath)];
      sim.push(cmd::upgradeBuilding(sim.tick(), bestSite, bestPath));
    }
  }

  // --- M2.9 strategies: shared helpers --------------------------------------

  float target = 0.0f;  // planet rotation the aiming bots are turning to
  std::vector<std::vector<int>> laneSlots;  // per planet lane, nearest slot first
  std::vector<int> batterySlots, fieldSlots, reservedSlots;
  bool laidOut = false;

  // `n` slots nearest to local angle `local`, alternating outwards.
  std::vector<int> ringAround(float local, int n) {
    const int total = static_cast<int>(world().slotCount());
    std::vector<int> out;
    if (total <= 0) return out;
    const float step = kTau / static_cast<float>(total);
    const int s0 = static_cast<int>(std::floor(wrapTau(local) / step + 0.5f)) % total;
    out.push_back(s0);
    for (int d = 1; static_cast<int>(out.size()) < n && d <= total / 2; ++d) {
      out.push_back((s0 + d) % total);
      if (static_cast<int>(out.size()) < n) out.push_back((s0 - d + total) % total);
    }
    return out;
  }

  void layout() {
    if (laidOut) return;
    laidOut = true;
    const World& w = world();
    for (const LaneDef& lane : w.planet->lanes) laneSlots.push_back(ringAround(lane.bearing, 6));
    batterySlots = ringAround(0.0f, 8);
    fieldSlots = ringAround(0.0f, 8);
    switch (strategy) {
      case Strategy::RotateToLane:
      case Strategy::RotateToWeakPoint:
      case Strategy::FaceTheThreat: reservedSlots = batterySlots; break;
      case Strategy::ChaseTheLight: reservedSlots = fieldSlots; break;
      default:
        for (const std::vector<int>& lane : laneSlots) {
          reservedSlots.insert(reservedSlots.end(), lane.begin(), lane.end());
        }
        break;
    }
  }

  bool isReserved(int slot) {
    for (int r : reservedSlots) if (r == slot) return true;
    return false;
  }

  uint32_t waveTableIndex() {
    const uint32_t wave = world().waveIndex;
    return wave < 8 ? wave : 7;
  }

  // Total hit points of an enemy definition down its layer chain.
  float totalHp(int enemy) {
    const EnemyDef& def = db.enemies[static_cast<size_t>(enemy)];
    return def.hp > 0.0f ? def.hp : 1.0f;
  }

  // Lane the coming wave puts the most hit points on.
  int heaviestLane() {
    const World& w = world();
    const WaveDef& wave = w.waveSet->waves[w.waveIndex];
    std::vector<float> load(w.planet->lanes.size(), 0.0f);
    for (const SpawnDef& sp : wave.spawns) {
      if (sp.lane < 0 || sp.lane >= static_cast<int>(load.size())) continue;
      if (db.enemies[static_cast<size_t>(sp.enemy)].behavior != EnemyBehavior::Lane) continue;
      load[static_cast<size_t>(sp.lane)] += static_cast<float>(sp.count) * totalHp(sp.enemy);
    }
    int best = 0;
    for (size_t i = 1; i < load.size(); ++i) {
      if (load[i] > load[static_cast<size_t>(best)]) best = static_cast<int>(i);
    }
    return best;
  }

  bool comingWaveHasCaster() {
    const World& w = world();
    for (const SpawnDef& sp : w.waveSet->waves[w.waveIndex].spawns) {
      if (db.enemies[static_cast<size_t>(sp.enemy)].shadowCaster) return true;
    }
    return false;
  }

  void sell(uint32_t slot) {
    World& w = world();
    const int type = w.slots[slot].type;
    if (type < 0) return;
    sim.push(cmd::sellBuilding(sim.tick(), static_cast<int32_t>(slot)));
    budget += db.buildings[static_cast<size_t>(type)].cost * w.planet->sellRefund;
  }

  // Fraction of the coming wave a surface point at world angle `angle` spends
  // in the beam, with the sun drifting on from where it is now.
  float litShare(float angle) {
    const World& w = world();
    const float drift = w.sun->speed * expectedWaveSeconds();
    const float half = 0.5f * litArc(w.planet->radius, w.sun->beamWidth);
    const float off = wrapPi(angle - w.sunBearing);  // ahead of the sun when positive
    if (std::fabs(drift) < 1.0e-4f) return std::fabs(off) <= half ? 1.0f : 0.0f;
    // Lit while the sun's travel so far is within `half` of `off`.
    const float lo = drift > 0.0f ? 0.0f : drift, hi = drift > 0.0f ? drift : 0.0f;
    const float from = off - half > lo ? off - half : lo;
    const float to = off + half < hi ? off + half : hi;
    return to > from ? (to - from) / (hi - lo) : 0.0f;
  }

  // Sells the panels the sun has left behind and tops the lit ones up to
  // `want`, assuming the planet will sit at rotation `ref` during the wave.
  // New panels go on the free slots that will be lit longest, the ones
  // furthest ahead of the sun first (they stay useful for later waves).
  void tendPanels(float ref, int want, bool buyNew) {
    World& w = world();
    const float half = 0.5f * litArc(w.planet->radius, w.sun->beamWidth);
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      if (w.slots[i].type != panel) continue;
      if (wrapPi(w.sunBearing - (w.slotAngle(i) + ref)) > half + 20.0f * kDeg) sell(i);
    }
    if (!buyNew) return;
    int lit = 0;
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      if (w.slots[i].type == panel && litShare(w.slotAngle(i) + ref) >= 0.5f) ++lit;
    }
    while (lit < want) {
      int best = -1;
      float bestScore = 0.6f;  // not worth a panel below 60% of the wave in the light
      for (uint32_t i = 0; i < w.slotCount(); ++i) {
        if (taken(static_cast<int>(i)) || isReserved(static_cast<int>(i))) continue;
        const float angle = w.slotAngle(i) + ref;
        const float score = litShare(angle) + 0.1f * wrapPi(angle - w.sunBearing);
        if (score > bestScore) {
          bestScore = score;
          best = static_cast<int>(i);
        }
      }
      if (best < 0 || !buy(best, panel)) break;
      ++lit;
    }
  }

  // --- M2.9 strategies: intermission plans ----------------------------------

  // The sensible build for any planet: towers under every lane (planet at
  // rotation 0), panels in the light, a Gatling under the sun for shadow
  // casters. `bank` adds bank-and-drill's economy.
  void planLanes(bool bank) {
    layout();
    World& w = world();
    const uint32_t k = waveTableIndex();
    const float drift = w.sun->speed * expectedWaveSeconds();
    const float centre = wrapTau(w.sunBearing + 0.5f * drift);
    const float half = 0.5f * litArc(w.planet->radius, w.sun->beamWidth);
    const bool dim = w.waveSet->waves[w.waveIndex].lightMult < 1.0f;

    // M2.4: a lane that brings weak-point enemies gets a Lance right under
    // it (these bots never aim it; the lane is where it does most good).
    if (precision >= 0) {
      for (const SpawnDef& sp : w.waveSet->waves[w.waveIndex].spawns) {
        if (!db.enemies[static_cast<size_t>(sp.enemy)].hasWeakPoint) continue;
        if (sp.lane < 0 || sp.lane >= static_cast<int>(laneSlots.size())) continue;
        const std::vector<int>& lane = laneSlots[static_cast<size_t>(sp.lane)];
        bool have = false;
        for (int slot : lane) have = have || (!free(slot) && w.slots[static_cast<size_t>(slot)].type == precision);
        for (int slot : lane) {
          if (have) break;
          if (taken(slot)) continue;
          have = buy(slot, precision);
          break;
        }
      }
    }

    for (size_t l = 0; l < laneSlots.size(); ++l) {
      const int want = l == 0 ? kWantFirstLane[k] : kWantOtherLane[k];
      while (towersOn(laneSlots[l]) < want && buyTower(laneSlots[l])) {}
    }
    // The anti-caster Gatling follows the sun.
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      if (w.slots[i].type != gatling || isReserved(static_cast<int>(i))) continue;
      if (wrapPi(w.sunBearing - w.slotAngle(i)) > half) sell(i);
    }
    if (comingWaveHasCaster()) {
      bool have = false;
      for (uint32_t i = 0; i < w.slotCount(); ++i) {
        have = have || (w.slots[i].type == gatling && std::fabs(wrapPi(w.slotAngle(i) - centre)) < 25.0f * kDeg);
      }
      if (!have) {
        const int slot = nearestFree(centre, reservedSlots, 25.0f * kDeg);
        if (slot >= 0) buy(slot, gatling);
      }
    }
    // Banking: a panel bought for a dim wave pays back slowly, so the money
    // is kept (or put into drills, which do not need light) and spent on guns
    // one intermission later.
    const bool hold = bank && dim;
    tendPanels(0.0f, kWantPanels[k], !hold);
    if (hold) {
      if (drill >= 0) {
        for (int n = 0; n < 2; ++n) {
          const int slot = nearestFree(wrapTau(w.sunBearing + kPi), reservedSlots, kPi);
          if (slot < 0 || !buy(slot, drill)) break;
        }
      }
      return;
    }
    for (int guard = 0; guard < 8; ++guard) {
      size_t least = 0;
      for (size_t l = 1; l < laneSlots.size(); ++l) {
        if (towersOn(laneSlots[l]) < towersOn(laneSlots[least])) least = l;
      }
      bool bought = buyTower(laneSlots[least]);
      for (size_t l = 0; !bought && l < laneSlots.size(); ++l) bought = buyTower(laneSlots[l]);
      if (!bought) break;
    }
  }

  // One battery of towers around local 0, turned to wherever it is needed;
  // panels go where the sun is while the battery faces the wave's main lane.
  void planBattery() {
    layout();
    World& w = world();
    const uint32_t k = waveTableIndex();
    const float ref = w.planet->lanes.empty() ? 0.0f : w.planet->lanes[static_cast<size_t>(heaviestLane())].bearing;
    target = wrapTau(ref);
    // M2.4: rotate-to-weak-point puts precision towers (Lances) in the middle
    // of its battery when weak-point enemies are coming: one, two from four.
    if (strategy == Strategy::RotateToWeakPoint && precision >= 0) {
      int weakUnits = 0;
      for (const SpawnDef& sp : w.waveSet->waves[w.waveIndex].spawns)
        if (db.enemies[static_cast<size_t>(sp.enemy)].hasWeakPoint) weakUnits += sp.count;
      const int wantLances = weakUnits == 0 ? 0 : (weakUnits >= 4 ? 2 : 1);
      int have = 0;
      for (int slot : batterySlots)
        if (!free(slot) && w.slots[static_cast<size_t>(slot)].type == precision) ++have;
      for (int slot : batterySlots) {
        if (have >= wantLances) break;
        if (taken(slot)) continue;
        if (!buy(slot, precision)) break;
        ++have;
      }
    }
    const int wantTowers = kWantFirstLane[k] + kWantOtherLane[k];
    while (towersOn(batterySlots) < wantTowers && buyTower(batterySlots)) {}
    tendPanels(ref, kWantPanels[k], true);
    for (int guard = 0; guard < 8; ++guard) {
      if (!buyTower(batterySlots)) break;
    }
  }

  // Chase-the-light: a panel field around local 0 that the rotation keeps under
  // the sun; towers are added on whatever slots face the lanes this wave.
  std::vector<int> fieldTowers;
  void planField() {
    layout();
    World& w = world();
    const uint32_t k = waveTableIndex();
    const float rotMid = w.sunBearing + 0.5f * w.sun->speed * expectedWaveSeconds();
    const float reach = 35.0f * kDeg;
    const size_t lanes = w.planet->lanes.size();
    auto covering = [&](size_t lane) {
      int n = 0;
      for (int slot : fieldTowers) {
        const float world = w.slotAngle(static_cast<uint32_t>(slot)) + rotMid;
        if (std::fabs(wrapPi(world - w.planet->lanes[lane].bearing)) < reach) ++n;
      }
      return n;
    };
    auto buyFor = [&](size_t lane) {
      const int slot = nearestFree(wrapTau(w.planet->lanes[lane].bearing - rotMid), reservedSlots, reach);
      if (slot < 0 || !buyTower(std::vector<int>{slot})) return false;
      fieldTowers.push_back(slot);
      return true;
    };
    // Guns the turning planet has carried away from every lane are sold.
    for (size_t t = 0; t < fieldTowers.size();) {
      const float world = w.slotAngle(static_cast<uint32_t>(fieldTowers[t])) + rotMid;
      bool useful = false;
      for (size_t l = 0; l < lanes; ++l) {
        useful = useful || std::fabs(wrapPi(world - w.planet->lanes[l].bearing)) < reach + 10.0f * kDeg;
      }
      if (useful) {
        ++t;
      } else {
        sell(static_cast<uint32_t>(fieldTowers[t]));
        fieldTowers.erase(fieldTowers.begin() + static_cast<std::ptrdiff_t>(t));
      }
    }
    // One gun on the wave's main lane, then light before more guns.
    if (lanes > 0) {
      const size_t heavy = static_cast<size_t>(heaviestLane());
      if (covering(heavy) < 1) buyFor(heavy);
    }
    while (towersOn(fieldSlots) < kWantFieldPanels[k]) {
      bool bought = false;
      for (int slot : fieldSlots) {
        if (taken(slot)) continue;
        bought = buy(slot, panel);
        break;
      }
      if (!bought) break;
    }
    for (size_t l = 0; l < lanes; ++l) {
      const int want = l == 0 ? kWantFirstLane[k] : kWantOtherLane[k];
      while (covering(l) < want && buyFor(l)) {}
    }
    for (int guard = 0; guard < 8 && lanes > 0; ++guard) {
      size_t least = 0;
      for (size_t l = 1; l < lanes; ++l) {
        if (covering(l) < covering(least)) least = l;
      }
      bool bought = buyFor(least);
      for (size_t l = 0; !bought && l < lanes; ++l) bought = buyFor(l);
      if (!bought) break;
    }
  }

  void plan() {
    budget = world().stardust;
    planned.clear();
    switch (strategy) {
      case Strategy::Never: break;
      case Strategy::Towers: planTowers(); break;
      case Strategy::Chaser: planChaser(); break;
      case Strategy::Sensible:
      case Strategy::Upgrader: planSensible(); break;
      case Strategy::NeverRotate: planLanes(false); break;
      case Strategy::BankAndDrill: planLanes(true); break;
      case Strategy::Balance: planLanes(false); break;
      case Strategy::RotateToLane:
      case Strategy::RotateToWeakPoint:
      case Strategy::FaceTheThreat: planBattery(); break;
      case Strategy::ChaseTheLight: planField(); break;
      case Strategy::Count: break;
    }
  }

  // --- M2.9 strategies: aiming ----------------------------------------------

  // How pressing an enemy at distance `dist` from the planet centre is, 0..1.
  float urgency(float dist, float horizon) {
    return clampf(1.0f - (dist - world().planet->radius) / horizon, 0.0f, 1.0f);
  }

  float landingDamage(const EnemyPool& e, uint32_t i) {
    const EnemyDef& def = db.enemies[e.type[i]];
    return def.land_damage(remainingLayers(def, e.hp[i]));
  }

  // rotate-to-lane: the battery faces the lane with the most landing damage
  // on its way (nearer counts more). Enemies that do not follow a lane are
  // not its business.
  void aimAtBusiestLane() {
    World& w = world();
    const size_t lanes = w.planet->lanes.size();
    if (lanes == 0 || w.phase != Phase::Wave) return;
    std::vector<float> load(lanes, 0.0f);
    const EnemyPool& e = w.enemies;
    for (uint32_t i = 0; i < e.count; ++i) {
      if ((e.flags[i] & kEnemyDead) != 0 || e.lane[i] >= lanes) continue;
      if (db.enemies[e.type[i]].behavior != EnemyBehavior::Lane) continue;
      const float u = urgency(std::sqrt(e.posX[i] * e.posX[i] + e.posY[i] * e.posY[i]), db.rules.spawnRadius);
      load[e.lane[i]] += landingDamage(e, i) * (0.1f + u * u);
    }
    size_t best = 0, current = lanes;
    for (size_t l = 0; l < lanes; ++l) {
      if (load[l] > load[best]) best = l;
      if (std::fabs(wrapPi(w.planet->lanes[l].bearing - target)) < 1.0f * kDeg) current = l;
    }
    if (load[best] <= 0.0f) return;
    // Do not flip-flop: leave the current lane only for a clearly busier one.
    if (current < lanes && best != current && load[best] < 1.25f * load[current]) return;
    target = wrapTau(w.planet->lanes[best].bearing);
  }

  // rotate-to-weak-point: with a precision tower built and a weak-point enemy
  // out there, turn so the tower sits right under it (weakPointHint, the
  // same answer the client's aim assist shows); otherwise the battery faces
  // the enemy nearest the planet.
  void aimAtNearestEnemy() {
    World& w = world();
    if (w.phase != Phase::Wave) return;
    WeakPointHint hint;
    if (weakPointHint(w, hint)) {
      target = wrapTau(w.planetRotation + hint.turn);
      return;
    }
    const EnemyPool& e = w.enemies;
    float bestD2 = 0.0f;
    bool found = false;
    float bx = 0.0f, by = 0.0f;
    for (uint32_t i = 0; i < e.count; ++i) {
      if ((e.flags[i] & kEnemyDead) != 0) continue;
      const float d2 = e.posX[i] * e.posX[i] + e.posY[i] * e.posY[i];
      if (!found || d2 < bestD2) {
        found = true;
        bestD2 = d2;
        bx = e.posX[i];
        by = e.posY[i];
      }
    }
    if (found) target = wrapTau(std::atan2(by, bx));
  }

  // Scored aiming (face-the-threat, balance): every few ticks, rate candidate
  // rotations by stardust per second from lit panels (x wLight) plus landing
  // damage inside tower arcs (x wThreat) and turn to the best.
  struct Cell {
    float x, y, value;
  };
  struct Gun {
    float local, aim, arc, range;
  };
  std::vector<Cell> cells;
  std::vector<Gun> guns;
  std::vector<float> panelAngles;
  float grid[kBearingBins * kHeightBins] = {};
  uint32_t lastScoreTick = 0;
  bool scored = false;

  float scoreRotation(float rot, float wLight, float wThreat) {
    World& w = world();
    const float radius = w.planet->radius;
    float light = 0.0f;
    if (wLight > 0.0f) {
      const float rate = db.buildings[static_cast<size_t>(panel)].panelRate * w.sun->baseStrength * w.lightMult;
      for (float a : panelAngles) {
        if (inBeam(radius, w.sun->beamWidth, w.sunBearing, a + rot)) light += rate;
      }
    }
    float threat = 0.0f;
    if (wThreat > 0.0f) {
      for (const Cell& c : cells) {
        float cover = 0.0f;
        for (const Gun& g : guns) {
          const float a = g.local + rot;
          const float tx = radius * std::cos(a), ty = radius * std::sin(a);
          const float dx = c.x - tx, dy = c.y - ty;
          const float d = std::sqrt(dx * dx + dy * dy);
          if (d > g.range + 250.0f) continue;
          const float ax = std::cos(a + g.aim), ay = std::sin(a + g.aim);
          if (d > 1.0e-3f && (dx * ax + dy * ay) / d < std::cos(g.arc)) continue;
          cover += d <= g.range ? 0.5f : 0.2f;  // in range, or about to be
          if (cover >= 1.0f) break;
        }
        threat += c.value * (cover < 1.0f ? cover : 1.0f);
      }
    }
    return wLight * light + wThreat * threat;
  }

  void aimByScore(float wLight, float wThreat) {
    World& w = world();
    if (scored && w.tick - lastScoreTick < 10) return;
    scored = true;
    lastScoreTick = w.tick;
    const float radius = w.planet->radius;
    guns.clear();
    panelAngles.clear();
    float maxRange = 0.0f;
    for (uint32_t i = 0; i < w.slotCount(); ++i) {
      const BuildingSlot& b = w.slots[i];
      if (b.type < 0) continue;
      const BuildingDef& def = db.buildings[static_cast<size_t>(b.type)];
      if (def.kind == BuildingKind::Tower) {
        guns.push_back({w.slotAngle(i), b.aimOffset, def.arc, def.range});
        if (def.range > maxRange) maxRange = def.range;
      } else if (b.type == panel) {
        panelAngles.push_back(w.slotAngle(i));
      }
    }
    cells.clear();
    if (wThreat > 0.0f && !guns.empty()) {
      for (float& g : grid) g = 0.0f;
      const float horizon = maxRange + 250.0f;
      const EnemyPool& e = w.enemies;
      for (uint32_t i = 0; i < e.count; ++i) {
        if ((e.flags[i] & kEnemyDead) != 0) continue;
        const float dist = std::sqrt(e.posX[i] * e.posX[i] + e.posY[i] * e.posY[i]);
        const float u = urgency(dist, horizon);
        if (u <= 0.0f) continue;
        int hb = static_cast<int>((dist - radius) / kHeightStep);
        hb = hb < 0 ? 0 : (hb >= kHeightBins ? kHeightBins - 1 : hb);
        int bb = static_cast<int>(wrapTau(std::atan2(e.posY[i], e.posX[i])) / kTau * kBearingBins);
        bb = bb < 0 ? 0 : (bb >= kBearingBins ? kBearingBins - 1 : bb);
        grid[bb * kHeightBins + hb] += landingDamage(e, i) * (0.25f + 0.75f * u);
      }
      for (int bb = 0; bb < kBearingBins; ++bb) {
        for (int hb = 0; hb < kHeightBins; ++hb) {
          const float v = grid[bb * kHeightBins + hb];
          if (v <= 0.0f) continue;
          const float a = (static_cast<float>(bb) + 0.5f) * kTau / kBearingBins;
          const float d = radius + (static_cast<float>(hb) + 0.5f) * kHeightStep;
          cells.push_back({d * std::cos(a), d * std::sin(a), v});
        }
      }
    }
    // A long turn costs a little, so ties go to the nearer rotation.
    auto rated = [&](float rot) {
      return scoreRotation(rot, wLight, wThreat) - 0.2f * std::fabs(wrapPi(rot - w.planetRotation));
    };
    const float current = rated(target);
    float best = current, bestRot = target;
    const int candidates = 36;
    for (int c = 0; c < candidates; ++c) {
      const float rot = static_cast<float>(c) * kTau / static_cast<float>(candidates);
      const float s = rated(rot);
      if (s > best) {
        best = s;
        bestRot = rot;
      }
    }
    // Hysteresis: only leave the current target for a clearly better one.
    if (best > current + 0.5f + 0.1f * std::fabs(current)) target = bestRot;
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
    if (strategy == Strategy::Sensible || strategy == Strategy::Upgrader ||
        strategy == Strategy::BankAndDrill) {
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
    switch (strategy) {
      case Strategy::ChaseTheLight:
        // The field sits around local 0: keep it centred under the sun.
        out = w.sunBearing;
        return true;
      case Strategy::RotateToLane:
        aimAtBusiestLane();
        out = target;
        return true;
      case Strategy::RotateToWeakPoint:
        aimAtNearestEnemy();
        out = target;
        return true;
      case Strategy::FaceTheThreat:
        aimByScore(0.0f, 1.0f);
        out = target;
        return true;
      case Strategy::Balance:
        aimByScore(1.0f, 1.0f);
        out = target;
        return true;
      default: break;
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

}  // namespace

float weakPointRotateInput(const World& world) {
  WeakPointHint hint;
  if (world.planet == nullptr || !weakPointHint(world, hint)) return 0.0f;
  const PlanetDef& p = *world.planet;
  const float dist = std::fabs(hint.turn);
  if (dist < 0.5f * kDeg) return 0.0f;  // lined up: let it settle
  // 70% of the fastest speed from which it can still stop in `dist`.
  float speed = 0.7f * std::sqrt(2.0f * p.rotAccel * dist);
  if (speed > p.rotMaxSpeed) speed = p.rotMaxSpeed;
  if (!(p.rotMaxSpeed > 0.0f)) return 0.0f;
  return clampf((hint.turn > 0.0f ? speed : -speed) / p.rotMaxSpeed, -1.0f, 1.0f);
}

RunResult play(const ContentDB& db, Strategy strategy, const RunOptions& options) {
  SimConfig cfg;
  cfg.seed = options.seed;
  cfg.content = &db;
  cfg.waveSet = options.waveSet;
  cfg.planetOverride = options.planet;  // M2.8
  Sim sim(cfg);
  World& w = sim.world();
  Bot bot(strategy, sim, db);
  RunResult r;
  r.hpMax = w.planetHpMax;
  r.wavesTotal = w.waveCount();
  r.leaksByEnemy.assign(db.enemies.size(), 0u);
  r.leakDamageByEnemy.assign(db.enemies.size(), 0.0f);
  r.killsByEnemy.assign(db.enemies.size(), 0u);
  r.waves.assign(w.waveCount(), WaveResult());
  const float start = w.stardust;
  float spent = 0.0f, refunded = 0.0f, bounty = 0.0f;
  bool planned = false;
  uint32_t settle = 0;
  uint32_t guard = 0;
  uint32_t current = 0;  // wave the events belong to
  while (w.phase != Phase::Won && w.phase != Phase::Lost && guard++ < 400000) {
    if (w.phase == Phase::Intermission) {
      if (w.waveIndex >= options.maxWaves) break;
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
          if (options.trace) {
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
      if (current < r.waves.size()) r.waves[current].seconds += w.dt;
    }
    sim.step();
    for (const Event& e : w.events.items) {
      const bool enemyOk = e.a >= 0 && static_cast<size_t>(e.a) < db.enemies.size();
      switch (e.type) {
        case EventType::WaveStarted:
          current = static_cast<uint32_t>(e.a);
          if (current < r.waves.size()) r.waves[current].started = true;
          break;
        case EventType::WaveCleared:
          if (e.a >= 0 && static_cast<size_t>(e.a) < r.waves.size()) r.waves[static_cast<size_t>(e.a)].cleared = true;
          break;
        case EventType::EnemyKilled:
          ++r.kills;
          bounty += e.value;
          if (enemyOk) ++r.killsByEnemy[static_cast<size_t>(e.a)];
          break;
        case EventType::PlanetHit:
          if (e.b >= 0 && e.b < static_cast<int32_t>(kMaxLanesTracked)) {
            ++r.leaks[e.b];
            if (w.waveIndex < kMaxWavesTracked) ++r.waveLeaks[w.waveIndex][e.b];
            r.leakDamage[e.b] += e.value;
          }
          if (enemyOk) {
            ++r.leaksByEnemy[static_cast<size_t>(e.a)];
            r.leakDamageByEnemy[static_cast<size_t>(e.a)] += e.value;
          }
          if (current < r.waves.size()) {
            ++r.waves[current].leaks;
            r.waves[current].hpLost += e.value;
          }
          break;
        case EventType::CommandRejected: ++r.rejected; break;
        case EventType::BuildingPlaced: spent += e.value; break;
        case EventType::BuildingUpgraded:  // M2.2
          spent += e.value;
          ++r.upgrades;
          break;
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
  r.spent = spent;
  r.stardust = w.stardust;
  r.ticks = w.tick;
  r.towers = static_cast<uint32_t>(bot.count(BuildingKind::Tower));
  r.panels = static_cast<uint32_t>(bot.count(BuildingKind::Panel));
  r.hash = sim.hash();
  return r;
}

RunResult play(const ContentDB& db, Strategy strategy, uint32_t seed, uint32_t maxWaves, bool trace) {
  RunOptions options;
  options.seed = seed;
  options.maxWaves = maxWaves;
  options.trace = trace;
  return play(db, strategy, options);
}

// --- threat coverage ----------------------------------------------------------

ThreatReport threatCoverage(const ContentDB& db, int waveSet, const std::vector<Strategy>& strategies,
                            const std::vector<RunResult>& runs, const ThreatOptions& options) {
  ThreatReport report;
  if (waveSet < 0 || static_cast<size_t>(waveSet) >= db.waveSets.size()) return report;
  const WaveSetDef& set = db.waveSets[static_cast<size_t>(waveSet)];
  const size_t n = runs.size() < strategies.size() ? runs.size() : strategies.size();
  for (size_t wave = 0; wave < set.waves.size(); ++wave) {
    WaveCoverage cover;
    cover.wave = set.waves[wave].wave;
    std::vector<std::string> families;
    for (Strategy s : allStrategies()) {
      if (!strategyIsAnswer(s)) continue;
      uint32_t played = 0, beaten = 0;
      for (size_t i = 0; i < n; ++i) {
        if (strategies[i] != s) continue;
        ++played;
        if (wave >= runs[i].waves.size()) continue;
        const WaveResult& wr = runs[i].waves[wave];
        if (wr.cleared && wr.hpLost <= options.maxHpLossFrac * runs[i].hpMax) ++beaten;
      }
      if (played == 0 || static_cast<float>(beaten) <= options.minSeedFrac * static_cast<float>(played)) continue;
      cover.beatenBy.push_back(s);
      bool known = false;
      for (const std::string& f : families) known = known || f == strategyFamily(s);
      if (!known) families.push_back(strategyFamily(s));
    }
    cover.families = static_cast<uint32_t>(families.size());
    cover.ok = cover.beatenBy.size() >= options.needed;
    if (!cover.ok) ++report.failing;
    report.waves.push_back(cover);
  }
  return report;
}

ThreatReport threatCoverage(const ContentDB& db, const std::vector<Strategy>& strategies,
                            const ThreatOptions& options) {
  std::vector<Strategy> who;
  std::vector<RunResult> runs;
  for (Strategy s : strategies) {
    for (uint32_t i = 0; i < options.seeds; ++i) {
      RunOptions run;
      run.seed = options.firstSeed + i;
      run.waveSet = options.waveSet;
      who.push_back(s);
      runs.push_back(play(db, s, run));
    }
  }
  return threatCoverage(db, options.waveSet, who, runs, options);
}

std::string formatThreatReport(const ThreatReport& report, const ThreatOptions& options) {
  std::string out = "| Wave | Answers | Families | Verdict | Beaten by |\n|---|---|---|---|---|\n";
  for (const WaveCoverage& c : report.waves) {
    out += "| " + std::to_string(c.wave) + " | " + std::to_string(c.beatenBy.size()) + " | " +
           std::to_string(c.families) + " | " + (c.ok ? "ok" : "TOO FEW") + " | ";
    for (size_t i = 0; i < c.beatenBy.size(); ++i) {
      if (i > 0) out += ", ";
      out += strategyId(c.beatenBy[i]);
    }
    if (c.beatenBy.empty()) out += "(none)";
    out += " |\n";
  }
  out += std::to_string(report.failing) + " of " + std::to_string(report.waves.size()) +
         " waves have fewer than " + std::to_string(options.needed) + " answers\n";
  return out;
}

}  // namespace orbitfall_bots
