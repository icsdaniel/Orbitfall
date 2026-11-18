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

enum class Shape : uint8_t { Circle, Triangle, Square, Hexagon, Star, Diamond, Disc };
// Support (M2.1): a building that neither shoots nor earns, e.g. an aura source.
enum class BuildingKind : uint8_t { Tower, Panel, Support, Count };
// Where a building may stand (M2.1): the ground ring only, the elevated ring
// only, or either.
enum class BuildingLayer : uint8_t { Ground, Elevated, Any };
// Stats an adjacency aura can change (M2.1).
enum class AuraStat : uint8_t { Range, FireRate, Damage, Income, Count };
enum class ProjectileKind : uint8_t { Dot, Shell };
enum class DamageType : uint8_t { Kinetic, Explosive, Energy, Cryo, Emp, Pierce, Count };
enum class TargetingMode : uint8_t { First, Strong, Close };
enum class SunPathType : uint8_t { ConstantDrift, Fixed };
enum class EnemyBehavior : uint8_t { Lane, Eclipser };

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

enum class UpgradeStat : uint8_t { Damage, FireRate, Range, Arc, Splash, Income, Count };
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
  // M2.4: a precision tower (the Lance) is the one the weak-point aim assist
  // and the rotate-to-weak-point bot line up. Data only; the weak-point rule
  // itself applies to every shot.
  bool precision = false;
  // Panels only.
  float panelRate = 0.0f;  // stardust per second at beamStrength 1
  std::vector<UpgradePathDef> paths;  // M2.2: empty = not upgradable
  VisualDef visual;
};

struct HoverDef {  // EnemyBehavior::Eclipser parameters
  float hoverRadius = 0.0f;
  float hoverSeconds = 0.0f;
  float descendSpeed = 0.0f;
  float spawnRadius = 0.0f;
  float spawnJitter = 0.0f;  // rad, +- around the lane bearing
};

// M2.4 weak point (PLAN §5.5, DECISIONS Q1). A hit is a weak-point hit when
// the firing tower's world angle at launch is within +-window of the enemy's
// bearing from the planet centre (the tower sits directly under it).
struct WeakPointDef {
  float window = 0.0f;    // rad; from rules.weakWindowDeg unless the enemy overrides it
  float mult = 3.5f;      // damage multiplier of a weak-point hit (3 to 4)
  float bodyMult = 0.25f; // damage multiplier of every other hit (floored at minDamageMult)
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
  // M2.4: +- spread around the lane bearing for a lane enemy, rad; < 0 uses
  // rules.spawnJitter. Bulwarks come in spread wide so a Lance fixed under the
  // lane catches only some of them: lining up is a rotation decision.
  float laneJitter = -1.0f;
  bool hasWeakPoint = false;  // M2.4: sets kTraitBulwark
  WeakPointDef weakPoint;
  bool hasHover = false;
  HoverDef hover;
  float damageMult[kDamageTypeCount] = {1, 1, 1, 1, 1, 1};
  VisualDef visual;

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

struct ForecastDef {
  int exactAhead = 1;
  int iconsAhead = 3;
};

struct WaveDef {
  int wave = 0;  // 1-based number
  float lightMult = 1.0f;
  float spawnSeconds = 0.0f;  // spawns are spread over this long
  std::vector<SpawnDef> spawns;
  ForecastDef forecast;
};

struct WaveSetDef {  // one file under content/waves/
  std::string id;
  int planet = kNoIndex;
  int sun = kNoIndex;
  float autoStartSeconds = 0.0f;  // 0 = off
  std::vector<WaveDef> waves;
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
  float weakWindow = 8.0f * 3.14159265358979323846f / 180.0f;  // M2.4: rad, default weak-point window
};

struct ContentDB {
  RulesDef rules;
  DamageRulesDef damage;  // M2.3
  std::vector<PlanetDef> planets;
  std::vector<SunDef> suns;
  std::vector<BuildingDef> buildings;
  std::vector<EnemyDef> enemies;
  std::vector<WaveSetDef> waveSets;

  // id -> index, kNoIndex when unknown.
  int planet_index(const std::string& id) const { return lookup(planetIds, id); }
  int sun_index(const std::string& id) const { return lookup(sunIds, id); }
  int building_index(const std::string& id) const { return lookup(buildingIds, id); }
  int enemy_index(const std::string& id) const { return lookup(enemyIds, id); }
  int wave_set_index(const std::string& id) const { return lookup(waveSetIds, id); }

  // Enemy indices from `enemy` down its popsInto chain, itself first
  // (drifter_green -> {green, blue, red}). Empty for an invalid index.
  std::vector<int> layer_chain(int enemy) const;

  // M2.8 home planets: true when `planet` can host wave set `waveSet` in place
  // of the wave set's own planet. Spawns hold lane INDICES resolved against the
  // wave set's planet, so the host must have the same lane ids in the same order.
  bool planet_fits_wave_set(int waveSet, int planet) const;

  std::unordered_map<std::string, int> planetIds, sunIds, buildingIds, enemyIds, waveSetIds;

 private:
  static int lookup(const std::unordered_map<std::string, int>& m, const std::string& id) {
    const auto it = m.find(id);
    return it == m.end() ? kNoIndex : it->second;
  }
};

}  // namespace orbitfall
