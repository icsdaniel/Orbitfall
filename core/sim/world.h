#pragma once
// World: all simulation state. Systems are free functions over World.
// Anything that affects future ticks must be included in hashWorld().
#include <cstdint>
#include <vector>

#include "content/content.h"
#include "sim/commands.h"
#include "sim/entity_pools.h"
#include "sim/events.h"
#include "sim/rng.h"
#include "spatial/grid.h"

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
  // Loaded content; must outlive the world. nullptr gives the bare M0 sim.
  const ContentDB* content = nullptr;
  // Wave set to play (index into content->waveSets); it names the planet and
  // the sun. kNoIndex (or no wave sets) gives a sandbox run on `planet`/`sun`.
  int waveSet = 0;
  int planet = 0;
  int sun = 0;
};

// Run phase; the values are the binding's get_phase().
enum class Phase : uint8_t { Intermission = 0, Wave = 1, Won = 2, Lost = 3 };

// Slot flag bits (BuildingSlot::flags), also the binding's get_slot_flags().
enum SlotFlag : uint8_t {
  kSlotLit = 1u << 0,        // inside the sun beam
  kSlotShaded = 1u << 1,     // inside the beam but in an enemy's shadow
  kSlotHasTarget = 1u << 2,  // a tower with an enemy in arc and range
};

// One entry per slot of the planet's ring. Buildings are few, so this is a
// plain array indexed by slot (BRIEF §5.3), in planet-local order: slot i sits
// at local angle 2*pi*i/N.
struct BuildingSlot {
  int16_t type = -1;       // index into ContentDB::buildings, -1 = empty
  uint8_t mode = 0;        // TargetingMode
  uint8_t flags = 0;       // SlotFlag bits, refreshed every tick
  float aimOffset = 0.0f;  // rad, added to the slot normal to get the aim axis
  float cooldown = 0.0f;   // seconds until the next shot
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

  // --- content (not hashed: fixed for the run) ---
  const ContentDB* content = nullptr;
  const PlanetDef* planet = nullptr;

  // --- planet ---
  float planetRotation = 0.0f;  // rad in [0, 2*pi); world angle = local + this
  float planetAngVel = 0.0f;    // rad/s

  float planetHp = 0.0f;
  float planetHpMax = 0.0f;

  // --- run flow ---
  Phase phase = Phase::Wave;
  uint32_t waveIndex = 0;       // 0-based: the wave being played, or the next one
  uint32_t enemiesSpawned = 0;  // total over the run

  // True while the wave clock runs: enemies, shots, sun and income advance.
  bool running() const { return content != nullptr && planet != nullptr && phase == Phase::Wave; }

  // --- economy and buildings ---
  float stardust = 0.0f;
  std::vector<BuildingSlot> slots;  // sized to the planet's slot count in initWorld

  uint32_t slotCount() const { return static_cast<uint32_t>(slots.size()); }
  // Local angle of a slot's centre.
  float slotAngle(uint32_t slot) const {
    return 6.28318530717958647692f * static_cast<float>(slot) / static_cast<float>(slots.size());
  }

  // --- determinism bookkeeping ---
  Rng rng;
  uint64_t commandsApplied = 0;
  uint64_t commandDigest = 0;  // running hash of every applied command

  // --- entity pools (fixed capacity from SimConfig, reserved in initWorld) ---
  EnemyPool enemies;
  ProjectilePool projectiles;

  // --- derived each tick (not hashed) ---
  SpatialGrid grid;              // enemies by position, rebuilt by spatialRebuild
  float maxEnemyRadius = 0.0f;   // largest EnemyDef::size, pads grid queries
  std::vector<uint32_t> slotTargets;  // per slot: enemy index picked by towerTargeting

  // --- queues (not hashed: pending input and undrained output) ---
  CommandQueue commands;
  EventQueue events;

  // Emits an event stamped with the current tick.
  void emit(EventType type, int32_t a = 0, int32_t b = 0, float x = 0.0f, float y = 0.0f,
            float value = 0.0f);
};

// Resets `world` to the start state for `config` and reserves all storage.
void initWorld(World& world, const SimConfig& config);

// Content-aware start of a run (what the binding's new_run calls): a world on
// wave set `waveSet` of `content` with default capacities. `content` must
// outlive the world. The 64-bit seed is folded to the sim's 32 bits.
void initWorld(World& world, const ContentDB& content, uint32_t waveSet, uint64_t seed);

// 64-bit hash of the deterministic state. Equal seed + command log => equal hash.
uint64_t hashWorld(const World& world);

}  // namespace orbitfall
