#include "systems/waves.h"

#include <cmath>

#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "systems/shadows.h"
#include "systems/sun.h"
#include "systems/systems.h"

namespace orbitfall {
namespace {

const WaveDef* currentWave(const World& world) {
  if (world.waveSet == nullptr || world.waveIndex >= world.waveSet->waves.size()) return nullptr;
  return &world.waveSet->waves[world.waveIndex];
}

}  // namespace

bool startWave(World& world) {
  const WaveDef* wave = currentWave(world);
  if (wave == nullptr || world.phase != Phase::Intermission) return false;
  world.phase = Phase::Wave;
  world.waveTime = 0.0f;
  world.intermissionTime = 0.0f;
  world.lightMult = wave->lightMult;
  for (uint16_t& n : world.spawned) n = 0;
  world.emit(EventType::WaveStarted, static_cast<int32_t>(world.waveIndex), wave->wave, 0.0f, 0.0f,
             wave->lightMult);
  return true;
}

void spawners(World& world, float dt) {
  if (!world.running()) return;
  const WaveDef* wave = currentWave(world);
  if (wave == nullptr) return;
  for (size_t s = 0; s < wave->spawns.size(); ++s) {
    const SpawnDef& spawn = wave->spawns[s];
    uint16_t& done = world.spawned[s];
    while (done < spawn.count && world.waveTime >= spawnTime(*wave, spawn.count, done)) {
      spawnEnemy(world, spawn.enemy, spawn.lane);  // a full pool drops the unit (overflowCount)
      ++done;
    }
  }
  world.waveTime += dt;
}

void waveFlow(World& world, float /*dt*/) {
  if (world.content == nullptr || world.waveSet == nullptr) return;
  if (world.phase == Phase::Intermission) {
    // Optional auto-start (off when autoStartSeconds is 0), in real ticks.
    if (world.waveSet->autoStartSeconds > 0.0f) {
      world.intermissionTime += kFixedDt;
      if (world.intermissionTime >= world.waveSet->autoStartSeconds) startWave(world);
    }
    return;
  }
  if (world.phase != Phase::Wave) return;
  const WaveDef* wave = currentWave(world);
  if (wave == nullptr || world.enemies.count > 0) return;
  for (size_t s = 0; s < wave->spawns.size(); ++s) {
    if (world.spawned[s] < wave->spawns[s].count) return;
  }

  world.emit(EventType::WaveCleared, static_cast<int32_t>(world.waveIndex), wave->wave);
  world.projectiles.clear();
  ++world.waveIndex;
  if (world.waveIndex >= world.waveSet->waves.size()) {
    world.phase = Phase::Won;
    world.emit(EventType::Victory, static_cast<int32_t>(world.waveIndex));
  } else {
    world.phase = Phase::Intermission;
    world.intermissionTime = 0.0f;
    world.lightMult = world.waveSet->waves[world.waveIndex].lightMult;  // preview the next light level
  }
}

std::vector<WaveForecast> forecast(const World& world, int count) {
  std::vector<WaveForecast> out;
  if (world.content == nullptr || world.waveSet == nullptr || world.planet == nullptr || count <= 0) {
    return out;
  }
  const std::vector<WaveDef>& waves = world.waveSet->waves;
  const size_t first = world.waveIndex + (world.phase == Phase::Wave ? 1u : 0u);
  if (first >= waves.size() || world.phase == Phase::Won || world.phase == Phase::Lost) return out;
  const ForecastDef& view = waves[first].forecast;
  const float radius = world.planet->radius;

  for (size_t k = 0; k < static_cast<size_t>(count) && first + k < waves.size(); ++k) {
    const WaveDef& wave = waves[first + k];
    WaveForecast f;
    f.wave = wave.wave;
    f.lightMult = wave.lightMult;
    f.exact = static_cast<int>(k) < view.exactAhead;
    if (static_cast<int>(k) < view.iconsAhead || f.exact) {
      for (const SpawnDef& spawn : wave.spawns) {
        const EnemyDef& def = world.content->enemies[static_cast<size_t>(spawn.enemy)];
        ForecastEntry e;
        e.lane = spawn.lane;
        e.laneBearing = world.planet->lanes[static_cast<size_t>(spawn.lane)].bearing;
        e.enemy = spawn.enemy;
        e.count = f.exact ? spawn.count : -1;
        e.shadowCaster = def.shadowCaster;
        f.entries.push_back(e);

        if (!f.exact || !def.shadowCaster || !def.hasHover || world.sun == nullptr) continue;
        // Ghost arc per unit: the sun where its path puts it when the unit
        // reaches its hover point, and the shadow it would park there.
        const float half = 0.5f * world.sun->beamWidth < radius ? 0.5f * world.sun->beamWidth : radius;
        for (int unit = 0; unit < spawn.count; ++unit) {
          const float travel = def.speed > 0.0f
                                   ? (def.hover.spawnRadius - def.hover.hoverRadius) / def.speed
                                   : 0.0f;
          const float arrival = spawnTime(wave, spawn.count, unit) + travel;
          float sigma = world.sunBearing;
          if (world.sunPath != nullptr) sigma = world.sunPath->advance(*world.sun, sigma, arrival);
          const float o = eclipserOffsetAt(world, def.size, sigma);
          const float lo = clampf(o - def.size, -half, half), hi = clampf(o + def.size, -half, half);
          f.eclipserGhostArcs.push_back(sigma + std::asin(lo / radius));
          f.eclipserGhostArcs.push_back(sigma + std::asin(hi / radius));
        }
      }
    }
    out.push_back(std::move(f));
  }
  return out;
}

uint64_t replayHash(const SimConfig& config, const std::vector<Command>& log, uint32_t ticks) {
  Sim sim(config);
  for (const Command& c : log) sim.push(c);
  for (uint32_t i = 0; i < ticks; ++i) sim.step();
  return sim.hash();
}

}  // namespace orbitfall
