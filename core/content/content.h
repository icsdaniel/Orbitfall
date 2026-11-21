#pragma once
// Content definitions and database (BRIEF §5.4). Plain data, no Godot includes.
//
// Content is authored as JSON under content/ (degrees in JSON) and loaded once
// into a ContentDB (radians in memory). Engine code refers to content by INDEX
// into the ContentDB vectors, never by literal id; ids exist only for lookups
// at the edges (UI, commands from GDScript, cross-references inside JSON).
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace orbitfall {

inline constexpr int kNoIndex = -1;
// BRIEF §3 "No hard counters": no damage multiplier may ever be below this,
// whatever content/rules.json says.
inline constexpr float kHardMinDamageMult = 0.25f;
// BRIEF §4.4: bent aim is limited to +-60 degrees.
inline constexpr float kMaxAimOffsetDeg = 60.0f;
// M2.6: a slow never takes more than this fraction of an enemy's speed.
inline constexpr float kMaxSlow = 0.9f;

enum class Shape : uint8_t { Circle, Triangle, Square, Hexagon, Star, Diamond, Disc };
// Support (M2.1): a building that neither shoots nor earns, e.g. an aura source.
// Shield (M2.10): protects an arc of sky from falling objects.
// Drill, Cooler (M2.7): light-independent income and its instability relief.
enum class BuildingKind : uint8_t { Tower, Panel, Support, Shield, Drill, Cooler, Count };
// Where a building may stand (M2.1): the ground ring only, the elevated ring
// only, or either.
enum class BuildingLayer : uint8_t { Ground, Elevated, Any };
// Stats an adjacency aura can change (M2.1).
// M2.6 appends Detect (reveals camo: removes that fraction of camo misses) and
// ShieldDamage (+x% damage dealt to enemy shields).
enum class AuraStat : uint8_t { Range, FireRate, Damage, Income, Detect, ShieldDamage, Count };
// M2.6: Beam = no projectile entity, a continuous beam on the target (Sun Lens).
enum class ProjectileKind : uint8_t { Dot, Shell, Beam };
enum class DamageType : uint8_t { Kinetic, Explosive, Energy, Cryo, Emp, Pierce, Count };
enum class TargetingMode : uint8_t { First, Strong, Close };
enum class SunPathType : uint8_t { ConstantDrift, Fixed };
enum class EnemyBehavior : uint8_t { Lane, Eclipser, Bomber };  // Bomber: M2.10

inline constexpr int kDamageTypeCount = static_cast<int>(DamageType::Count);
// M2.3: the JSON name of a damage type ("kinetic"), for the codex and UI.
const char* damage_type_name(DamageType type);

// Trait bits (BRIEF §4.6). EnemyDef::traits is an OR of these.
enum Trait : uint32_t {
  kTraitCamo = 1u << 0,
  kTraitArmored = 1u << 1,
  kTraitShielded = 1u << 2,
  kTraitFast = 1u << 3,
  kTraitRegrow = 1u << 4,
  kTraitSplitter = 1u << 5,
  kTraitHeatResistant = 1u << 6,
  kTraitBurrower = 1u << 7,
  kTraitOrbiter = 1u << 8,
  kTraitHealer = 1u << 9,
  kTraitBulwark = 1u << 10,
  kTraitCarrier = 1u << 11,
  // M2.3. Set by the loader on every enemy with more than one layer; appended
  // so the M1 bit values (and the shader's bit tests) stay as they were.
  kTraitLayered = 1u << 12,
};
inline constexpr int kTraitCount = 13;

// M2.3 soft-counter matrix (content/damage.json, PLAN §5.5). One row per trait
// bit: the damage-type multipliers an enemy with that trait takes, plus the
// texts the codex shows. Every multiplier is >= kHardMinDamageMult.
struct TraitDef {
  std::string id;          // JSON name of the bit ("armored")
  std::string name;        // display name
  std::string cue;         // how it is drawn
  std::string effect;      // what it does
  std::string easierWith;  // what answers it
  float mult[kDamageTypeCount] = {1, 1, 1, 1, 1, 1};
  // M2.6: fraction of the shots of a tower that does not detect it that miss
  // (camo: "about half of undetected shots miss"); 0 for every other trait.
  float missChance = 0.0f;
};

struct DamageRulesDef {
  TraitDef traits[kTraitCount];  // index = trait bit number
  // Multiplier on damage dealt to a shield (the Shielded "first X absorbed").
  float shieldMult[kDamageTypeCount] = {1, 1, 1, 1, 1, 1};
};

struct VisualDef {
  Shape shape = Shape::Circle;
  std::string color;  // palette key, e.g. "tower.cannon", "tier.red"
  // Health colour tier for "tier.<name>" keys (0 red, 1 blue, 2 green, 3 yellow,
  // 4 pink, 5 black, 6 white, 7 ceramic; BRIEF §4.6), -1 for any other key.
  int colorTier = -1;
  bool trail = false;
};

struct LaneDef {
  std::string id;
  float bearing = 0.0f;  // radians, world angle the lane comes in from
};

struct PlanetDef {
  std::string id;
  std::string name;  // M2.8: display name for the planet picker (defaults to the id)
  float radius = 0.0f;
  int slots = 0;
  float hp = 0.0f;
  float rotMaxSpeed = 0.0f;  // rad/s
  float rotAccel = 0.0f;     // rad/s^2
  float rotDamping = 0.0f;   // 1/s
  float startStardust = 0.0f;
  std::vector<LaneDef> lanes;
  float surfaceTimeScale = 1.0f;
  float sellRefund = 0.0f;  // fraction of cost refunded
  // M2.1: a second ring of slots above the ground ring. Elevated slot i needs
  // a building on ground slot i; buildings there stand `elevatedHeight` above
  // the surface.
  bool elevatedLayer = true;
  float elevatedHeight = 10.0f;
  VisualDef visual;

  int lane_index(const std::string& laneId) const {
    for (size_t i = 0; i < lanes.size(); ++i)
      if (lanes[i].id == laneId) return static_cast<int>(i);
    return kNoIndex;
  }
};

struct SunDef {
  std::string id;
  SunPathType type = SunPathType::ConstantDrift;
  float start = 0.0f;  // rad
  float speed = 0.0f;  // rad/s (sim time)
  float beamWidth = 0.0f;  // world units
  float baseStrength = 1.0f;
};

struct ProjectileDef {
  ProjectileKind kind = ProjectileKind::Dot;
  float speed = 0.0f;
  float damage = 0.0f;
  DamageType type = DamageType::Kinetic;
  float splash = 0.0f;  // radius, 0 = none
  // M2.6 (optional, default off):
  int pierce = 0;            // extra enemies a shot passes through (Railgun)
  float slow = 0.0f;         // speed taken off what it hits, 0..0.9 (Cryo)
  float slowSeconds = 0.0f;  // how long the slow lasts
};

// M2.6 Sun Lens beam (ProjectileKind::Beam): damage per second
// = projectile.damage x ramp x (channelFloor + (1 - channelFloor) x channel),
// where the ramp climbs from rampStart to 1 over rampSeconds on one target and
// channel is the lit, unshaded fraction of panel capacity (World::channel).
struct BeamDef {
  float rampSeconds = 1.5f;
  float rampStart = 0.3f;
  float channelFloor = 0.2f;
};

// M2.6: an active ability fired by the ActivateBuilding command. It lasts
// `seconds` and multiplies damage by `damageMult`; it then needs `cooldown`
// seconds, counted rules.lightChargeMult times faster while the site is lit
// and unshaded (light-charged actives, PLAN 5.4).
struct ActiveDef {
  std::string id;  // "overcharge"
  std::string name;
  float cooldown = 0.0f;
  float seconds = 0.0f;
  float damageMult = 1.0f;
};

struct FalloffPoint {
  float t = 0.0f;     // distance as a fraction of range, 0..1
  float mult = 1.0f;  // damage multiplier at t (>= minDamageMult)
};

// Adjacency aura (M2.1): every other building whose footprint is within
// `radius` slots along the ring (either layer) and whose kind is in `kinds`
// gets `stat` multiplied by 1 + the sum of the amounts reaching it.
struct AuraDef {
  AuraStat stat = AuraStat::Range;
  float amount = 0.0f;  // +0.2 = +20%; may be negative
  int radius = 1;       // k: reach in slots from the edge of the footprint
  uint32_t kinds = ~0u;  // bit per BuildingKind it affects
};

// --- M2.2 upgrade paths -------------------------------------------------------
// A building may declare up to kUpgradePaths paths of up to kMaxUpgradeTiers
// tiers. Each tier lists stat modifiers and a stardust cost. With every bought
// tier of both paths taken together:
//   effective stat = (base + sum of "add" values) * product of "mul" values
// so the order tiers were bought in never matters (systems/upgrades.h).
inline constexpr int kUpgradePaths = 2;
inline constexpr int kMaxUpgradeTiers = 4;

// M2.6 appends: Slow (projectile slow), Pierce (extra enemies), AuraAmount
// (multiplier on every aura amount, base 1), AuraRadius (slots added to every
// aura radius, base 0), ActiveCooldown (seconds), ActivePower (active damageMult).
// M2.7 appends: Instability (drill weight in the instability curve) and Cooling
// (Cooler); Income is panelRate on panels and drillRate on drills.
enum class UpgradeStat : uint8_t {
  Damage, FireRate, Range, Arc, Splash, Income,
  Slow, Pierce, AuraAmount, AuraRadius, ActiveCooldown, ActivePower,
  Instability, Cooling, Count
};
inline constexpr int kUpgradeStatCount = static_cast<int>(UpgradeStat::Count);
enum class ModOp : uint8_t { Add, Mul };

struct StatModifier {
  UpgradeStat stat = UpgradeStat::Damage;
  ModOp op = ModOp::Mul;
  float value = 1.0f;  // Arc + Add is in radians in memory (degrees in JSON)
};

struct UpgradeTierDef {
  std::string name;
  float cost = 0.0f;  // stardust
  std::vector<StatModifier> mods;
};

struct UpgradePathDef {
  std::string id;
  std::string name;
  std::vector<UpgradeTierDef> tiers;  // tiers[0] is tier 1
};

// M2.10 Shield building (PLAN §5.4b): a barrier over an arc of the sky above
// its slot. Falling objects that come down inside the arc and under the dome
// are absorbed and drain `energy`; at 0 the shield goes offline for
// `rechargeSeconds` (charged lightChargeMult times faster while the slot is lit
// and unshaded), then comes back full.
struct ShieldDef {
  float width = 0.0f;            // rad, FULL width of the protected arc (centred on the slot)
  float height = 0.0f;           // world units above the surface: the dome radius is R + height
  float energy = 0.0f;           // energy when full; an absorbed object costs its damage
  float rechargeSeconds = 0.0f;  // offline time at 1x charge rate
  float regenPerSec = 0.0f;      // energy back per second while online (also light-charged)
  float upkeepPerSec = 0.0f;     // stardust per second while online (DESIGN Q23; default 0 = off)
};

struct BuildingDef {
  std::string id;
  BuildingKind kind = BuildingKind::Tower;
  int footprint = 1;  // slots covered (1..3): the anchor slot and the next ones
  BuildingLayer layer = BuildingLayer::Ground;  // where it may be built
  std::vector<AuraDef> auras;
  float cost = 0.0f;
  bool aimMount = false;
  float aimOffset = 0.0f;  // rad, default mount offset from the slot normal
  // Towers only.
  float arc = 0.0f;  // rad, half-angle around the aim axis
  float range = 0.0f;
  float fireRate = 0.0f;  // shots per second
  bool hasProjectile = false;
  ProjectileDef projectile;
  std::vector<FalloffPoint> falloff;
  std::vector<TargetingMode> targeting;  // first entry is the default mode
  // Panels only.
  float panelRate = 0.0f;  // stardust per second at beamStrength 1
  // M2.7 drills: stardust per second whatever the light, and the drill's weight
  // in the instability curve (RulesDef::instability).
  float drillRate = 0.0f;
  float instability = 1.0f;
  // M2.7 coolers: instability removed from the global level; times
  // litCoolingMult while the cooler's slot is lit and unshaded (a radiator
  // works best on the night side).
  float cooling = 0.0f;
  float litCoolingMult = 1.0f;
  std::vector<UpgradePathDef> paths;  // M2.2: empty = not upgradable
  // Shields only (M2.10).
  ShieldDef shield;
  // M2.6 (optional): beam numbers (projectile.kind == Beam) and an active.
  BeamDef beam;
  bool hasActive = false;
  ActiveDef active;
  std::string name;  // display name; the id capitalised when absent
  VisualDef visual;
};

// M2.10 falling object (content/falling/*.json): meteor, bomb. Falls straight
// down a fixed world bearing at `speed`; on reaching the surface it subtracts
// `damage` planet HP (buildings are never damaged).
struct FallingDef {
  std::string id;
  float damage = 0.0f;
  float speed = 0.0f;        // world units per second, radial
  float size = 0.0f;         // radius
  float spawnRadius = 0.0f;  // distance wave volleys appear at (0: only dropped, e.g. bombs)
  VisualDef visual;
};

struct BomberDef {  // EnemyBehavior::Bomber parameters (M2.10)
  float orbitRadius = 0.0f;  // it stops its approach here and circles the planet
  float orbitSpeed = 0.0f;   // rad/s around the planet (sign = direction)
  float dropSeconds = 0.0f;  // one bomb every this many seconds while circling
  int bombs = 0;             // then it dives at the planet
  int bomb = kNoIndex;       // index into ContentDB::falling
};

struct HoverDef {  // EnemyBehavior::Eclipser parameters
  float hoverRadius = 0.0f;
  float hoverSeconds = 0.0f;
  float descendSpeed = 0.0f;
  float spawnRadius = 0.0f;
  float spawnJitter = 0.0f;  // rad, +- around the lane bearing
};

struct EnemyDef {
  std::string id;
  EnemyBehavior behavior = EnemyBehavior::Lane;
  Shape shape = Shape::Circle;
  int layers = 1;
  float hpPerLayer = 1.0f;
  float hp = 1.0f;  // total; layers * hpPerLayer unless "hp" is given
  float speed = 0.0f;
  float size = 0.0f;  // radius
  float landDamagePerLayer = 0.0f;
  float landDamage = 0.0f;       // flat value for non-layered units
  bool flatLandDamage = false;   // true: use landDamage, else per layer
  float bounty = 0.0f;
  uint32_t traits = 0;
  bool shadowCaster = false;
  bool hpBar = false;
  int hpBarSegments = 1;  // M2.3: segments of the HP bar of a large unit
  float shield = 0.0f;    // M2.3: Shielded, damage absorbed before any HP is lost
  int popsInto = kNoIndex;  // enemy index this becomes when a layer pops
  bool hasHover = false;
  HoverDef hover;
  bool hasBomber = false;  // M2.10
  BomberDef bomber;
  float damageMult[kDamageTypeCount] = {1, 1, 1, 1, 1, 1};
  VisualDef visual;
  // --- M2.5 enemies batch 1 ---
  // Regrow (Spore): after regrowSeconds without taking any damage the unit
  // regains one layer (steps back up its popsInto chain), never above the type
  // it spawned as. 0 = no regrow. JSON: "regrow": {"seconds": s}.
  float regrowSeconds = 0.0f;
  // Splitter: killed by damage (not by landing), it splits into splitCount
  // units of splitInto, spread evenly across +-splitSpread world units sideways
  // to its path. JSON: "split": {"into": id, "count": n, "spread": u}.
  int splitInto = kNoIndex;
  int splitCount = 0;
  float splitSpread = 0.0f;

  // BRIEF §6 landing damage.
  float land_damage(int remainingLayers) const {
    return flatLandDamage ? landDamage : landDamagePerLayer * static_cast<float>(remainingLayers);
  }
};

struct SpawnDef {
  int lane = kNoIndex;   // index into the wave set's planet lanes
  int enemy = kNoIndex;  // index into ContentDB::enemies
  int count = 0;
};

// M2.10: a volley of falling objects in a wave: `count` objects released
// evenly from startSeconds over `seconds`, each on bearing +- spread (seeded).
struct VolleyDef {
  int falling = kNoIndex;  // index into ContentDB::falling
  float bearing = 0.0f;    // rad, world
  float spread = 0.0f;     // rad, +- around the bearing
  int count = 0;
  float startSeconds = 0.0f;
  float seconds = 0.0f;
};

struct ForecastDef {
  int exactAhead = 1;
  int iconsAhead = 3;
};

struct WaveDef {
  int wave = 0;  // 1-based number
  float lightMult = 1.0f;
  float spawnSeconds = 0.0f;  // spawns are spread over this long
  std::vector<SpawnDef> spawns;
  std::vector<VolleyDef> volleys;  // M2.10 ("falling" in JSON)
  ForecastDef forecast;
};

struct WaveSetDef {  // one file under content/waves/
  std::string id;
  int planet = kNoIndex;
  int sun = kNoIndex;
  float autoStartSeconds = 0.0f;  // 0 = off
  float startStardust = -1.0f;    // M2.10: < 0 = the planet's startStardust (demo sets may start richer)
  std::vector<WaveDef> waves;
};

// M2.7 instability (rules.json "instability", optional; without it drills
// never overheat). Global level = max(0, curve(load) - cooling), where load is
// the sum of the instability weights of the running drills and cooling the
// sum of the coolers' cooling. While the level is above `threshold`, every
// running drill gains heat at riseBase + risePerExcess * (level - threshold)
// per second; at heat 1 it overheats and stops for overheatSeconds (heat back
// to 0 when it restarts). At or below the threshold heat falls at decayPerSec.
// Nothing is ever damaged.
struct CurvePoint {
  float x = 0.0f;
  float y = 0.0f;
};
struct InstabilityDef {
  std::vector<CurvePoint> curve;  // x ascending; linear between points, extended past the last
  float threshold = 1.0f;
  float riseBase = 0.1f;
  float risePerExcess = 0.5f;
  float decayPerSec = 0.1f;
  float overheatSeconds = 10.0f;
};

// M2.11 bent aim (rules.json "aim", optional; DESIGN Q33 default "re-aim
// between waves"). Aim-mounted towers fire along slot normal + offset. Without
// the block any offset within +-kMaxAimOffsetDeg is allowed and SetAim is free
// at any time (the M1 behaviour).
struct AimRulesDef {
  std::vector<float> offsets;        // rad, ascending; offsets snap to the nearest (empty = free)
  float reaimCostFrac = 0.0f;        // SetAim costs this fraction of the building's cost
  bool reaimBetweenWavesOnly = false;  // SetAim refused (WrongPhase) while a wave runs
};

// M2.11 focus fire (rules.json "focus", optional; DESIGN Q34). Where the aim
// axes of two or more aim-mounted towers cross within their ranges a focus
// point forms in the planet frame. A hit inside its zone (radius `radius`)
// from a feeding tower deals x(1 + min(bonusPerExtra x (feeders - 1), cap)).
// Two or more beam towers (Sun Lens) feeding one zone merge into one beam
// beyond it with summed power that pierces. Without the block there is no bonus
// and no merge (points are still solved, for the UI).
struct FocusRulesDef {
  float bonusPerExtra = 0.0f;  // +25% = 0.25 per feeding tower beyond the first
  float cap = 0.0f;            // total bonus cap (0.5 = +50%)
  float radius = 50.0f;        // zone radius; crossings closer than this merge into one point
  float minAltitude = 10.0f;   // crossings lower than this above the surface are ignored
  bool lensMerge = false;
  // Towers feeding a zone pick targets inside it before any other (then by
  // their targeting mode), as players expect focus fire to do (toy D7).
  bool preferZone = false;
};

struct RulesDef {  // content/rules.json: global tunables
  int tickRate = 30;
  float killBounty = 1.0f;  // multiplier on EnemyDef::bounty
  float minDamageMult = kHardMinDamageMult;
  float lightChargeMult = 2.0f;
  float spawnRadius = 0.0f;   // lane enemies spawn at this distance
  float spawnJitter = 0.0f;   // rad, +- around the lane bearing for lane enemies
  float surfaceTimeEaseSeconds = 0.0f;
  float elevatedRangeBonus = 0.15f;  // M2.1: +15% range on the elevated ring
  // M2.2: once any path of a building is above this tier, every other path
  // is capped at it (default 2: one path may reach the top, the other stops at 2).
  int upgradeExclusiveTier = 2;
  float fallingWarnSeconds = 3.0f;  // M2.10: falling objects are telegraphed this long before impact
  InstabilityDef instability;  // M2.7
  AimRulesDef aim;      // M2.11
  FocusRulesDef focus;  // M2.11
};

struct ContentDB {
  RulesDef rules;
  DamageRulesDef damage;  // M2.3
  std::vector<PlanetDef> planets;
  std::vector<SunDef> suns;
  std::vector<BuildingDef> buildings;
  std::vector<EnemyDef> enemies;
  std::vector<WaveSetDef> waveSets;
  std::vector<FallingDef> falling;  // M2.10

  // id -> index, kNoIndex when unknown.
  int planet_index(const std::string& id) const { return lookup(planetIds, id); }
  int sun_index(const std::string& id) const { return lookup(sunIds, id); }
  int building_index(const std::string& id) const { return lookup(buildingIds, id); }
  int enemy_index(const std::string& id) const { return lookup(enemyIds, id); }
  int wave_set_index(const std::string& id) const { return lookup(waveSetIds, id); }
  int falling_index(const std::string& id) const { return lookup(fallingIds, id); }  // M2.10

  // Enemy indices from `enemy` down its popsInto chain, itself first
  // (drifter_green -> {green, blue, red}). Empty for an invalid index.
  std::vector<int> layer_chain(int enemy) const;

  // M2.8 home planets: true when `planet` can host wave set `waveSet` in place
  // of the wave set's own planet. Spawns hold lane INDICES resolved against the
  // wave set's planet, so the host must have the same lane ids in the same order.
  bool planet_fits_wave_set(int waveSet, int planet) const;

  std::unordered_map<std::string, int> planetIds, sunIds, buildingIds, enemyIds, waveSetIds;
  std::unordered_map<std::string, int> fallingIds;  // M2.10

 private:
  static int lookup(const std::unordered_map<std::string, int>& m, const std::string& id) {
    const auto it = m.find(id);
    return it == m.end() ? kNoIndex : it->second;
  }
};

}  // namespace orbitfall
