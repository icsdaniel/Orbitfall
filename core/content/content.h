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
enum class BuildingKind : uint8_t { Tower, Panel };
enum class BuildingLayer : uint8_t { Ground, Elevated };
enum class ProjectileKind : uint8_t { Dot, Shell };
enum class DamageType : uint8_t { Kinetic, Explosive, Energy, Cryo, Emp, Pierce, Count };
enum class TargetingMode : uint8_t { First, Strong, Close };
enum class SunPathType : uint8_t { ConstantDrift, Fixed };
enum class EnemyBehavior : uint8_t { Lane, Eclipser };

inline constexpr int kDamageTypeCount = static_cast<int>(DamageType::Count);

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

struct BuildingDef {
  std::string id;
  BuildingKind kind = BuildingKind::Tower;
  int footprint = 1;
  BuildingLayer layer = BuildingLayer::Ground;
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
  VisualDef visual;
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
  int popsInto = kNoIndex;  // enemy index this becomes when a layer pops
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
};

struct ContentDB {
  RulesDef rules;
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

  std::unordered_map<std::string, int> planetIds, sunIds, buildingIds, enemyIds, waveSetIds;

 private:
  static int lookup(const std::unordered_map<std::string, int>& m, const std::string& id) {
    const auto it = m.find(id);
    return it == m.end() ? kNoIndex : it->second;
  }
};

}  // namespace orbitfall
