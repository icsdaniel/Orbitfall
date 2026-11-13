#pragma once
// M1.9 waves (sim part): the run flow and the forecast.
//
// Flow: a run on a wave set starts in Phase::Intermission. The sim is frozen
// there (no sun drift, enemies, shots or income) but still accepts build,
// sell, aim and rotate. StartWave begins wave `waveIndex`; its spawns are
// spread evenly over spawnSeconds per entry. When everything has spawned and
// no enemy is left the wave is cleared: WaveCleared, then the next
// intermission, or Victory after the last wave.
#include <cstdint>
#include <vector>

#include "sim/commands.h"
#include "sim/world.h"

namespace orbitfall {

// Starts the next wave. False (nothing happens) outside an intermission.
bool startWave(World& world);

// Sim time into the wave at which unit `k` (0-based) of a spawn entry with
// `count` units appears. The spawner and the forecast both use this.
inline float spawnTime(const WaveDef& wave, int count, int k) {
  return count > 0 ? wave.spawnSeconds * static_cast<float>(k) / static_cast<float>(count) : 0.0f;
}

// --- forecast (BRIEF §4.8; "the forecast never lies") ---

struct ForecastEntry {
  int lane = kNoIndex;       // lane index on the planet
  float laneBearing = 0.0f;  // rad, world angle the lane comes in from
  int enemy = kNoIndex;      // index into ContentDB::enemies
  int count = 0;             // -1 when this wave is not shown exactly
  bool shadowCaster = false;
};

struct WaveForecast {
  int wave = 0;  // 1-based wave number
  float lightMult = 1.0f;
  bool exact = false;                  // counts and ghost arcs are given
  std::vector<ForecastEntry> entries;  // empty beyond the icon range: light level only
  // Pairs of world angles [a0, a1]: where each hovering shadow caster of an
  // exact wave is expected to park its shadow, from the sun's path up to its
  // arrival and the panels as they stand now.
  std::vector<float> eclipserGhostArcs;
};

// Up to `count` upcoming waves: from the next wave in an intermission, from
// the one after the current wave during a wave. Reads the same WaveDef data as
// the spawner. How far ahead waves are exact / icons comes from the first
// upcoming wave's forecast block.
std::vector<WaveForecast> forecast(const World& world, int count);

// --- replay ---

// Runs a fresh world on `config` with `log` for `ticks` ticks and returns the
// state hash. With world.commandLog of a finished run this reproduces its hash.
uint64_t replayHash(const SimConfig& config, const std::vector<Command>& log, uint32_t ticks);

}  // namespace orbitfall
