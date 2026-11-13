#pragma once
// World: all simulation state. Systems are free functions over World.
// Anything that affects future ticks must be included in hashWorld().
#include <cstdint>

#include "sim/commands.h"
#include "sim/entity_pools.h"
#include "sim/events.h"
#include "sim/rng.h"

namespace orbitfall {

constexpr int kTickRate = 30;
constexpr float kFixedDt = 1.0f / static_cast<float>(kTickRate);
constexpr float kMaxTimeScale = 8.0f;

// Load-time settings. Capacities are reserved once in initWorld.
struct SimConfig {
  uint32_t seed = 1;
  uint32_t commandCapacity = 1024;  // pending commands before the queue grows
  uint32_t eventCapacity = 1024;    // undrained events before the queue grows
  uint32_t enemyCapacity = 16384;       // fixed; spawns beyond it are refused
  uint32_t projectileCapacity = 32768;  // fixed; shots beyond it are refused
};

struct World {
  // --- clock ---
  uint32_t seed = 0;
  uint32_t tick = 0;     // ticks completed; a command stamped with this tick applies in the next step
  double simTime = 0.0;  // scaled seconds simulated so far
  float dt = 0.0f;       // scaled dt of the current tick, set by applyCommands

  // --- time scale (0 = paused). Eases from `From` to `Target` over easeTicksTotal ticks. ---
  float timeScale = 1.0f;
  float timeScaleFrom = 1.0f;
  float timeScaleTarget = 1.0f;
  uint32_t easeTicksLeft = 0;
  uint32_t easeTicksTotal = 0;

  // --- latest player input ---
  float rotateInput = 0.0f;  // [-1, 1], consumed by planet rotation (M1.1)

  // --- determinism bookkeeping ---
  Rng rng;
  uint64_t commandsApplied = 0;
  uint64_t commandDigest = 0;  // running hash of every applied command

  // --- entity pools (fixed capacity from SimConfig, reserved in initWorld) ---
  EnemyPool enemies;
  ProjectilePool projectiles;

  // --- queues (not hashed: pending input and undrained output) ---
  CommandQueue commands;
  EventQueue events;

  // Emits an event stamped with the current tick.
  void emit(EventType type, int32_t a = 0, int32_t b = 0, float x = 0.0f, float y = 0.0f,
            float value = 0.0f);
};

// Resets `world` to the start state for `config` and reserves all storage.
void initWorld(World& world, const SimConfig& config);

// 64-bit hash of the deterministic state. Equal seed + command log => equal hash.
uint64_t hashWorld(const World& world);

}  // namespace orbitfall
