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
#include "systems/shadows.h"

namespace orbitfall {

constexpr int kTickRate = 30;
constexpr float kFixedDt = 1.0f / static_cast<float>(kTickRate);
constexpr float kMaxTimeScale = 8.0f;

// Load-time settings. Capacities are reserved once in initWorld.
struct SimConfig {
  uint32_t seed = 1;
  uint32_t commandCapacity = 1024;  // pending commands before the queue grows
  uint32_t eventCapacity = 1024;    // undrained events before the queue grows
  uint32_t commandLogCapacity = 16384;  // logged commands before the replay log grows
  uint32_t enemyCapacity = 16384;       // fixed; spawns beyond it are refused
  uint32_t projectileCapacity = 32768;  // fixed; shots beyond it are refused
  uint32_t fallingCapacity = 4096;      // M2.10: fixed; falling objects beyond it are refused
  // Loaded content; must outlive the world. nullptr gives the bare M0 sim.
  const ContentDB* content = nullptr;
  // Wave set to play (index into content->waveSets); it names the planet and
  // the sun. kNoIndex (or no wave sets) gives a sandbox run on `planet`/`sun`.
  int waveSet = 0;
  int planet = 0;
  int sun = 0;
  // M2.8 home planets: with a wave set, play it on this planet instead of the
  // wave set's own (kNoIndex = the wave set's planet). Ignored unless
  // ContentDB::planet_fits_wave_set says the planet has the wave set's lanes.
  int planetOverride = kNoIndex;
};

// Run phase; the values are the binding's get_phase().
enum class Phase : uint8_t { Intermission = 0, Wave = 1, Won = 2, Lost = 3 };

// Slot flag bits (BuildingSlot::flags), also the binding's get_slot_flags().
enum SlotFlag : uint8_t {
  kSlotLit = 1u << 0,        // inside the sun beam
  kSlotShaded = 1u << 1,     // inside the beam but in an enemy's shadow
  kSlotHasTarget = 1u << 2,  // a tower with an enemy in arc and range
  kSlotShieldOffline = 1u << 3,  // M2.10: a shield at 0 energy, recharging
};

// One entry per slot of the planet's ring. Buildings are few, so this is a
// plain array indexed by slot (BRIEF §5.3), in planet-local order: slot i sits
// at local angle 2*pi*i/N.
//
// M2.1: a building of footprint w anchored on slot i covers slots i..i+w-1
// (wrapping). Only the anchor holds `type`; every covered slot holds `anchor`.
// A "site" index addresses both rings: site = slot + layer * ringSlots, so the
// ground ring keeps its M1 indices and elevated slot i is site N + i.
struct BuildingSlot {
  int16_t type = -1;       // index into ContentDB::buildings, -1 = empty or not the anchor
  uint8_t mode = 0;        // TargetingMode
  uint8_t flags = 0;       // SlotFlag bits, refreshed every tick
  float aimOffset = 0.0f;  // rad, added to the slot normal to get the aim axis
  float cooldown = 0.0f;   // seconds until the next shot
  int16_t anchor = -1;     // site of the building covering this slot, -1 = free
  uint8_t width = 0;       // anchors only: the building's footprint
  // Derived by recomputeLayout whenever the layout changes (not hashed):
  // elevated bonus and adjacency auras, as multipliers on the definition.
  float rangeMult = 1.0f;
  float fireRateMult = 1.0f;
  float damageMult = 1.0f;
  float incomeMult = 1.0f;
  uint8_t tiers[kUpgradePaths] = {0, 0};  // M2.2: bought tiers per upgrade path (anchors)
  // M2.10 shields: energy left, and recharge progress (seconds at 1x) while offline.
  float energy = 0.0f;
  float charge = 0.0f;
  // M2.7 drills (anchors): heat 0..1 (overheats at 1) and the seconds left of
  // an overheat shutdown (0 = running). Never damage: only uptime.
  float heat = 0.0f;
  float overheat = 0.0f;
};

// One adjacency link (M2.1), for the Build view: `source` buffs `target`.
struct AuraLink {
  uint16_t source = 0;  // site of the aura building
  uint16_t target = 0;  // site of the building it affects
  uint8_t stat = 0;     // AuraStat
  float amount = 0.0f;
};

struct SunPath;

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
  const SunDef* sun = nullptr;
  const SunPath* sunPath = nullptr;  // how the bearing moves (systems/sun.h)
  const WaveSetDef* waveSet = nullptr;  // nullptr = sandbox: always in Phase::Wave, no spawner

  // --- sun ---
  float sunBearing = 0.0f;       // rad in [0, 2*pi), world angle the light comes from
  float lightMult = 1.0f;        // the wave's light level
  float beamStrength = 0.0f;     // sun baseStrength * lightMult, set by sunlight
  float incomePerSecond = 0.0f;  // stardust/s from lit unshaded panels right now
  float drillIncomePerSecond = 0.0f;  // M2.7: stardust/s from running drills right now

  // --- M2.7 instability (derived every tick by the drills system, not hashed) ---
  float instability = 0.0f;      // global level, max(0, curve(load) - cooling)
  float instabilityLoad = 0.0f;  // summed instability weight of the running drills
  float cooling = 0.0f;          // summed cooling of the coolers right now

  // --- planet ---
  float planetRotation = 0.0f;  // rad in [0, 2*pi); world angle = local + this
  float planetAngVel = 0.0f;    // rad/s

  float planetHp = 0.0f;
  float planetHpMax = 0.0f;

  // --- run flow ---
  Phase phase = Phase::Wave;
  uint32_t waveIndex = 0;       // 0-based: the wave being played, or the next one
  uint32_t enemiesSpawned = 0;  // total over the run
  float waveTime = 0.0f;          // sim seconds since the wave started
  float intermissionTime = 0.0f;  // real seconds in this intermission (auto-start)
  std::vector<uint16_t> spawned;  // per spawn entry of the current wave: units out so far
  std::vector<uint16_t> volleysSpawned;  // M2.10: per volley of the current wave: objects out so far

  uint32_t waveCount() const { return waveSet ? static_cast<uint32_t>(waveSet->waves.size()) : 0u; }

  // True while the wave clock runs: enemies, shots, sun and income advance.
  bool running() const { return content != nullptr && planet != nullptr && phase == Phase::Wave; }

  // --- economy and buildings ---
  float stardust = 0.0f;
  std::vector<BuildingSlot> slots;  // ground ring, sized to the planet's slot count in initWorld
  std::vector<BuildingSlot> elevated;  // M2.1 elevated ring: same size, or empty without that layer
  std::vector<AuraLink> auraLinks;     // derived by recomputeLayout (not hashed)

  // Slots per ring.
  uint32_t slotCount() const { return static_cast<uint32_t>(slots.size()); }
  // Local angle of a slot's centre.
  float slotAngle(uint32_t slot) const {
    return 6.28318530717958647692f * static_cast<float>(slot) / static_cast<float>(slots.size());
  }

  // Sites: both rings in one index space (ground first).
  uint32_t siteCount() const { return static_cast<uint32_t>(slots.size() + elevated.size()); }
  bool siteElevated(uint32_t site) const { return site >= slots.size(); }
  BuildingSlot& site(uint32_t i) { return i < slots.size() ? slots[i] : elevated[i - slots.size()]; }
  const BuildingSlot& site(uint32_t i) const {
    return i < slots.size() ? slots[i] : elevated[i - slots.size()];
  }
  // Local angle of a site: the centre of the footprint for an anchor, else the
  // centre of the slot.
  float siteAngle(uint32_t i) const {
    const uint32_t n = slotCount();
    const uint8_t width = site(i).width;
    const float extra = width > 1 ? 0.5f * static_cast<float>(width - 1) : 0.0f;
    return 6.28318530717958647692f * (static_cast<float>(i % n) + extra) / static_cast<float>(n);
  }
  // Distance of a site from the planet's centre.
  float siteRadius(uint32_t i) const {
    return planet->radius + (siteElevated(i) ? planet->elevatedHeight : 0.0f);
  }

  // --- determinism bookkeeping ---
  Rng rng;
  uint64_t commandsApplied = 0;
  uint64_t commandDigest = 0;  // running hash of every applied command

  // --- entity pools (fixed capacity from SimConfig, reserved in initWorld) ---
  EnemyPool enemies;
  ProjectilePool projectiles;
  FallingPool falling;  // M2.10

  // --- derived each tick (not hashed) ---
  SpatialGrid grid;              // enemies by position, rebuilt by spatialRebuild
  float maxEnemyRadius = 0.0f;   // largest EnemyDef::size, pads grid queries
  std::vector<ShadeInterval> shade;   // merged shadows in beam space, sorted
  uint32_t shadeCount = 0;
  std::vector<uint32_t> slotTargets;  // per slot: enemy index picked by towerTargeting

  // --- replay log (not hashed): every applied command, stamped with the tick
  // it applied on. seed + this log reproduces the run (systems/waves.h). ---
  bool recordCommands = true;
  std::vector<Command> commandLog;

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
// `planet` (M2.8) picks a home planet other than the wave set's (kNoIndex = its own).
void initWorld(World& world, const ContentDB& content, uint32_t waveSet, uint64_t seed,
               int planet = kNoIndex);

// 64-bit hash of the deterministic state. Equal seed + command log => equal hash.
uint64_t hashWorld(const World& world);

}  // namespace orbitfall
