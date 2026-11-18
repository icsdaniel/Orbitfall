// JSON -> ContentDB with validation. Exception-free (the GDExtension builds
// with -fno-exceptions). Every problem becomes one line in `errors`.
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "content/content_loader.h"
#include "systems/upgrades.h"
#include "third_party/json/mini_json.h"

namespace orbitfall {

std::vector<int> ContentDB::layer_chain(int enemy) const {
  std::vector<int> chain;
  const int n = static_cast<int>(enemies.size());
  while (enemy >= 0 && enemy < n && static_cast<int>(chain.size()) < n) {
    chain.push_back(enemy);
    enemy = enemies[static_cast<size_t>(enemy)].popsInto;
  }
  return chain;
}

namespace {

using mini_json::Value;

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = std::numeric_limits<double>::infinity();
float deg2rad(double deg) { return static_cast<float>(deg * kPi / 180.0); }

template <typename E>
struct Named {
  const char* name;
  E value;
};

constexpr Named<Shape> kShapes[] = {
    {"circle", Shape::Circle},   {"triangle", Shape::Triangle}, {"square", Shape::Square},
    {"hexagon", Shape::Hexagon}, {"star", Shape::Star},         {"diamond", Shape::Diamond},
    {"disc", Shape::Disc}};
constexpr Named<BuildingKind> kBuildingKinds[] = {{"tower", BuildingKind::Tower},
                                                  {"panel", BuildingKind::Panel},
                                                  {"support", BuildingKind::Support}};
constexpr Named<BuildingLayer> kBuildingLayers[] = {{"ground", BuildingLayer::Ground},
                                                    {"elevated", BuildingLayer::Elevated},
                                                    {"any", BuildingLayer::Any}};
constexpr Named<AuraStat> kAuraStats[] = {{"range", AuraStat::Range},
                                          {"fireRate", AuraStat::FireRate},
                                          {"damage", AuraStat::Damage},
                                          {"income", AuraStat::Income}};
constexpr Named<ProjectileKind> kProjectileKinds[] = {{"dot", ProjectileKind::Dot},
                                                      {"shell", ProjectileKind::Shell}};
constexpr Named<DamageType> kDamageTypes[] = {
    {"kinetic", DamageType::Kinetic}, {"explosive", DamageType::Explosive},
    {"energy", DamageType::Energy},   {"cryo", DamageType::Cryo},
    {"emp", DamageType::Emp},         {"pierce", DamageType::Pierce}};
constexpr Named<TargetingMode> kTargetingModes[] = {{"first", TargetingMode::First},
                                                    {"strong", TargetingMode::Strong},
                                                    {"close", TargetingMode::Close}};
constexpr Named<SunPathType> kSunPathTypes[] = {{"constant_drift", SunPathType::ConstantDrift},
                                                {"fixed", SunPathType::Fixed}};
constexpr Named<EnemyBehavior> kEnemyBehaviors[] = {{"lane", EnemyBehavior::Lane},
                                                    {"eclipser", EnemyBehavior::Eclipser}};
// M2.2: stats an upgrade tier may modify, and how.
constexpr Named<UpgradeStat> kUpgradeStats[] = {
    {"damage", UpgradeStat::Damage}, {"fireRate", UpgradeStat::FireRate},
    {"range", UpgradeStat::Range},   {"arc", UpgradeStat::Arc},
    {"splash", UpgradeStat::Splash}, {"income", UpgradeStat::Income}};
constexpr Named<ModOp> kModOps[] = {{"add", ModOp::Add}, {"mul", ModOp::Mul}};
constexpr Named<uint32_t> kTraits[] = {
    {"camo", kTraitCamo},         {"armored", kTraitArmored},
    {"shielded", kTraitShielded}, {"fast", kTraitFast},
    {"regrow", kTraitRegrow},     {"splitter", kTraitSplitter},
    {"heat_resistant", kTraitHeatResistant}, {"burrower", kTraitBurrower},
    {"orbiter", kTraitOrbiter},   {"healer", kTraitHealer},
    {"bulwark", kTraitBulwark},   {"carrier", kTraitCarrier},
    {"layered", kTraitLayered}};

int trait_bit_index(uint32_t bit) {
  int i = 0;
  while (i < kTraitCount - 1 && (1u << i) != bit) ++i;
  return i;
}

template <typename E, size_t N>
bool lookup_named(const Named<E> (&table)[N], const std::string& name, E& out) {
  for (const auto& e : table) {
    if (name == e.name) {
      out = e.value;
      return true;
    }
  }
  return false;
}

template <typename E, size_t N>
std::string names_of(const Named<E> (&table)[N]) {
  std::string s;
  for (const auto& e : table) {
    if (!s.empty()) s += ", ";
    s += e.name;
  }
  return s;
}

// "tier.<name>" palette keys are the health colour tiers, in BRIEF §4.6 order.
int color_tier(const std::string& key) {
  static const char* const kTiers[] = {"red", "blue", "green", "yellow", "pink", "black", "white", "ceramic"};
  if (key.compare(0, 5, "tier.") != 0) return -1;
  for (int i = 0; i < 8; ++i)
    if (key.compare(5, std::string::npos, kTiers[i]) == 0) return i;
  return -1;
}

std::string fmt(double v) {
  std::string s = std::to_string(v);
  while (s.size() > 1 && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

// Numeric bound: value must be > or >= `min`, and <= `max`.
struct Range {
  double min;
  bool exclusiveMin;
  double max;
};
constexpr Range kAny{-kInf, false, kInf};
constexpr Range kNonNeg{0.0, false, kInf};
constexpr Range kPositive{0.0, true, kInf};
constexpr Range kUnit{0.0, false, 1.0};

// A JSON object being read, with the context needed for error messages.
class Obj {
 public:
  Obj(const Value* v, std::string where, std::vector<std::string>& errors)
      : v_(v), where_(std::move(where)), errors_(errors) {}

  bool valid() const { return v_ != nullptr && v_->is_object(); }
  const std::string& where() const { return where_; }
  bool has(const char* key) const {
    const Value* f = v_->find(key);
    return f != nullptr && !f->is_null();
  }

  void error(const std::string& msg) const { errors_.push_back(where_ + ": " + msg); }
  void field_error(const char* key, const std::string& msg) const {
    error(std::string("'") + key + "': " + msg);
  }

  // Required field of the given type; reports "missing field" or a type error.
  const Value* get(const char* key, mini_json::Type type) const {
    const Value* f = v_->find(key);
    if (f == nullptr) {
      error(std::string("missing field '") + key + "'");
      return nullptr;
    }
    if (f->type != type) {
      field_error(key, std::string("expected ") + mini_json::type_name(type) + ", got " +
                           mini_json::type_name(f->type));
      return nullptr;
    }
    return f;
  }

  bool check_range(const char* key, double v, const Range& r) const {
    if (!std::isfinite(v)) {
      field_error(key, "must be finite");
      return false;
    }
    if (v < r.min || (r.exclusiveMin && v <= r.min)) {
      const bool negative = v < 0.0 && r.min >= 0.0;
      field_error(key, std::string(negative ? "must not be negative: " : "") + "must be " +
                           (r.exclusiveMin ? "> " : ">= ") + fmt(r.min) + ", got " + fmt(v));
      return false;
    }
    if (v > r.max) {
      field_error(key, "must be <= " + fmt(r.max) + ", got " + fmt(v));
      return false;
    }
    return true;
  }

  bool num(const char* key, double& out, const Range& r) const {
    const Value* f = get(key, mini_json::Type::Number);
    if (f == nullptr || !check_range(key, f->number, r)) return false;
    out = f->number;
    return true;
  }
  bool num(const char* key, float& out, const Range& r) const {
    double d = 0.0;
    if (!num(key, d, r)) return false;
    out = static_cast<float>(d);
    return true;
  }
  // Degrees in JSON -> radians in memory.
  bool angle(const char* key, float& out, const Range& rDeg) const {
    double d = 0.0;
    if (!num(key, d, rDeg)) return false;
    out = deg2rad(d);
    return true;
  }
  bool integer(const char* key, int& out, const Range& r) const {
    double d = 0.0;
    if (!num(key, d, r)) return false;
    if (d != std::floor(d) || std::fabs(d) > 1e9) {
      field_error(key, "must be a whole number, got " + fmt(d));
      return false;
    }
    out = static_cast<int>(d);
    return true;
  }
  bool str(const char* key, std::string& out) const {
    const Value* f = get(key, mini_json::Type::String);
    if (f == nullptr) return false;
    if (f->string.empty()) {
      field_error(key, "must not be empty");
      return false;
    }
    out = f->string;
    return true;
  }
  bool boolean(const char* key, bool& out) const {
    const Value* f = get(key, mini_json::Type::Bool);
    if (f == nullptr) return false;
    out = f->boolean;
    return true;
  }
  template <typename E, size_t N>
  bool named(const char* key, const Named<E> (&table)[N], E& out) const {
    std::string s;
    if (!str(key, s)) return false;
    if (!lookup_named(table, s, out)) {
      field_error(key, "unknown value '" + s + "' (expected one of: " + names_of(table) + ")");
      return false;
    }
    return true;
  }
  const Value* array(const char* key) const { return get(key, mini_json::Type::Array); }
  Obj object(const char* key) const {
    return Obj(get(key, mini_json::Type::Object), where_ + ": " + key, errors_);
  }
  // Element `i` of array field `key`, expected to be an object.
  Obj element(const char* key, const Value& arr, size_t i) const {
    const std::string w = where_ + ": " + key + "[" + std::to_string(i) + "]";
    if (!arr.array[i].is_object()) {
      errors_.push_back(w + ": expected object, got " + mini_json::type_name(arr.array[i].type));
      return Obj(nullptr, w, errors_);
    }
    return Obj(&arr.array[i], w, errors_);
  }

 private:
  const Value* v_;
  std::string where_;
  std::vector<std::string>& errors_;
};

// References by id are resolved after every file has been read.
struct PendingEnemy {
  std::string where;
  std::string popsInto;
  bool weakWindowSet = false;  // M2.4: else rules.weakWindowDeg applies
};
struct PendingSpawn {
  std::string lane, enemy;
};
struct PendingWaveSet {
  std::string where;
  std::string planet, sun;
  std::vector<std::vector<PendingSpawn>> spawns;  // per wave
};

struct Loader {
  ContentDB db;
  std::vector<std::string>& errors;
  bool haveRules = false;
  bool haveDamage = false;
  std::string damageWhere = "damage.json";
  std::vector<PendingEnemy> pendingEnemies;
  std::vector<PendingWaveSet> pendingWaveSets;
  std::vector<std::string> buildingWhere;

  explicit Loader(std::vector<std::string>& errs) : errors(errs) {}

  // Reads "id", checks it matches the file name and is unique.
  bool read_id(const Obj& o, const std::string& stem, std::unordered_map<std::string, int>& ids,
               size_t index, std::string& out) {
    if (!o.str("id", out)) return false;
    if (out != stem) o.field_error("id", "'" + out + "' must match the file name '" + stem + "'");
    if (!ids.emplace(out, static_cast<int>(index)).second) {
      o.field_error("id", "duplicate id '" + out + "'");
      return false;
    }
    return true;
  }

  void read_visual(const Obj& parent, VisualDef& out, bool needShape) {
    const Obj v = parent.object("visual");
    if (!v.valid()) return;
    if (needShape) v.named("shape", kShapes, out.shape);
    v.str("color", out.color);
    out.colorTier = color_tier(out.color);
    if (v.has("trail")) v.boolean("trail", out.trail);
  }

  void read_rules(const Obj& o) {
    haveRules = true;
    RulesDef& r = db.rules;
    o.integer("tickRate", r.tickRate, Range{1.0, false, 1000.0});
    o.num("killBounty", r.killBounty, kNonNeg);
    o.num("minDamageMult", r.minDamageMult, Range{kHardMinDamageMult, false, 1.0});
    o.num("lightChargeMult", r.lightChargeMult, kPositive);
    o.num("spawnRadius", r.spawnRadius, kPositive);
    o.angle("spawnJitterDeg", r.spawnJitter, Range{0.0, false, 180.0});
    o.num("surfaceTimeEaseSeconds", r.surfaceTimeEaseSeconds, kNonNeg);
    if (o.has("elevatedRangeBonus")) o.num("elevatedRangeBonus", r.elevatedRangeBonus, Range{-0.9, false, 10.0});
    if (o.has("upgradeExclusiveTier"))  // M2.2
      o.integer("upgradeExclusiveTier", r.upgradeExclusiveTier,
                Range{0.0, false, static_cast<double>(kMaxUpgradeTiers)});
    if (o.has("weakWindowDeg")) o.angle("weakWindowDeg", r.weakWindow, Range{0.0, true, 45.0});
  }

  // {"kinetic": 0.5, ...} -> out[type]. The floor is checked in resolve().
  void read_type_mults(const Obj& parent, const char* key, float* out) {
    const Obj m = parent.object(key);
    if (!m.valid()) return;
    const Value* raw = parent.get(key, mini_json::Type::Object);
    for (const auto& kv : raw->object) {
      DamageType type = DamageType::Kinetic;
      if (!lookup_named(kDamageTypes, kv.first, type)) {
        m.error("unknown damage type '" + kv.first + "'");
        continue;
      }
      m.num(kv.first.c_str(), out[static_cast<int>(type)], kNonNeg);
    }
  }

  // M2.3: content/damage.json, the soft-counter matrix and trait legend.
  void read_damage(const Obj& o) {
    haveDamage = true;
    DamageRulesDef& d = db.damage;
    if (o.has("shieldMult")) read_type_mults(o, "shieldMult", d.shieldMult);
    const Obj traits = o.object("traits");
    if (!traits.valid()) return;
    const Value* raw = o.get("traits", mini_json::Type::Object);
    for (const auto& kv : raw->object) {
      uint32_t bit = 0;
      if (!lookup_named(kTraits, kv.first, bit)) {
        traits.error("unknown trait '" + kv.first + "'");
        continue;
      }
      const Obj t = traits.object(kv.first.c_str());
      if (!t.valid()) continue;
      TraitDef& def = d.traits[trait_bit_index(bit)];
      t.str("name", def.name);
      t.str("cue", def.cue);
      t.str("effect", def.effect);
      if (t.has("easierWith")) t.str("easierWith", def.easierWith);
      if (t.has("mult")) read_type_mults(t, "mult", def.mult);
    }
  }

  void read_planet(const Obj& o, const std::string& stem) {
    PlanetDef p;
    if (!read_id(o, stem, db.planetIds, db.planets.size(), p.id)) return;
    o.num("radius", p.radius, kPositive);
    o.integer("slots", p.slots, Range{1.0, false, 4096.0});
    o.num("hp", p.hp, kPositive);
    o.angle("rotMaxSpeed", p.rotMaxSpeed, kPositive);
    o.angle("rotAccel", p.rotAccel, kPositive);
    o.num("rotDamping", p.rotDamping, kNonNeg);
    o.num("startStardust", p.startStardust, kNonNeg);
    o.num("surfaceTimeScale", p.surfaceTimeScale, Range{0.0, true, 1.0});
    o.num("sellRefund", p.sellRefund, kUnit);
    // M2.1 (optional): the elevated ring; its height defaults to 10% of the radius.
    if (o.has("elevatedLayer")) o.boolean("elevatedLayer", p.elevatedLayer);
    p.elevatedHeight = 0.1f * p.radius;
    if (o.has("elevatedHeight")) o.num("elevatedHeight", p.elevatedHeight, kNonNeg);
    if (const Value* lanes = o.array("lanes")) {
      if (lanes->array.empty()) o.field_error("lanes", "must not be empty");
      for (size_t i = 0; i < lanes->array.size(); ++i) {
        const Obj l = o.element("lanes", *lanes, i);
        if (!l.valid()) continue;
        LaneDef lane;
        l.angle("bearingDeg", lane.bearing, kAny);
        if (!l.str("id", lane.id)) continue;
        if (p.lane_index(lane.id) != kNoIndex) {
          l.field_error("id", "duplicate lane id '" + lane.id + "'");
          continue;
        }
        p.lanes.push_back(lane);
      }
    }
    if (o.has("visual")) read_visual(o, p.visual, true);
    db.planets.push_back(std::move(p));
  }

  void read_sun(const Obj& o, const std::string& stem) {
    SunDef s;
    if (!read_id(o, stem, db.sunIds, db.suns.size(), s.id)) return;
    o.named("type", kSunPathTypes, s.type);
    o.angle("startDeg", s.start, kAny);
    o.angle("speedDegPerSec", s.speed, kAny);
    o.num("beamWidth", s.beamWidth, kPositive);
    o.num("baseStrength", s.baseStrength, kNonNeg);
    db.suns.push_back(std::move(s));
  }

  void read_building(const Obj& o, const std::string& stem) {
    BuildingDef b;
    if (!read_id(o, stem, db.buildingIds, db.buildings.size(), b.id)) return;
    const bool kindOk = o.named("kind", kBuildingKinds, b.kind);
    // M1 files give both; optional since M2.1 (1 slot, ground only).
    if (o.has("footprint")) o.integer("footprint", b.footprint, Range{1.0, false, 3.0});
    if (o.has("layer")) o.named("layer", kBuildingLayers, b.layer);
    read_auras(o, b);
    o.num("cost", b.cost, kNonNeg);
    o.boolean("aimMount", b.aimMount);
    if (o.has("aimOffsetDeg"))
      o.angle("aimOffsetDeg", b.aimOffset, Range{-kMaxAimOffsetDeg, false, kMaxAimOffsetDeg});
    read_visual(o, b.visual, true);

    if (kindOk && b.kind == BuildingKind::Tower) {
      o.angle("arcDeg", b.arc, Range{0.0, true, 180.0});
      o.num("range", b.range, kPositive);
      o.num("fireRate", b.fireRate, kPositive);
      const Obj pr = o.object("projectile");
      if (pr.valid()) {
        b.hasProjectile = true;
        pr.named("kind", kProjectileKinds, b.projectile.kind);
        pr.num("speed", b.projectile.speed, kPositive);
        pr.num("damage", b.projectile.damage, kNonNeg);
        pr.named("type", kDamageTypes, b.projectile.type);
        if (pr.has("splash")) pr.num("splash", b.projectile.splash, kNonNeg);
      }
      if (const Value* t = o.array("targeting")) {
        if (t->array.empty()) o.field_error("targeting", "a tower needs at least one mode");
        for (const Value& m : t->array) {
          TargetingMode mode = TargetingMode::First;
          if (!m.is_string() || !lookup_named(kTargetingModes, m.string, mode)) {
            o.field_error("targeting", "unknown targeting mode '" + m.string +
                                           "' (expected one of: " + names_of(kTargetingModes) + ")");
            continue;
          }
          b.targeting.push_back(mode);
        }
      }
      read_falloff(o, b);
      if (o.has("precision")) o.boolean("precision", b.precision);  // M2.4
    } else if (kindOk && b.kind == BuildingKind::Panel) {
      o.num("panelRate", b.panelRate, kNonNeg);
    }
    if (kindOk && o.has("paths")) read_paths(o, b);  // M2.2
    buildingWhere.push_back(o.where());
    db.buildings.push_back(std::move(b));
  }

  // "auras": [{ "stat", "amount", "radius", "affects": [kinds] }] (M2.1,
  // optional). No "affects" = every kind of building.
  void read_auras(const Obj& o, BuildingDef& b) {
    if (!o.has("auras")) return;
    const Value* list = o.array("auras");
    if (list == nullptr) return;
    for (size_t i = 0; i < list->array.size(); ++i) {
      const Obj a = o.element("auras", *list, i);
      if (!a.valid()) continue;
      AuraDef aura;
      a.named("stat", kAuraStats, aura.stat);
      a.num("amount", aura.amount, Range{-0.9, false, 10.0});
      a.integer("radius", aura.radius, Range{0.0, false, 4096.0});
      if (a.has("affects")) {
        aura.kinds = 0;
        if (const Value* kinds = a.array("affects")) {
          if (kinds->array.empty()) a.field_error("affects", "must not be empty");
          for (const Value& k : kinds->array) {
            BuildingKind kind = BuildingKind::Tower;
            if (!k.is_string() || !lookup_named(kBuildingKinds, k.string, kind)) {
              a.field_error("affects", "unknown building kind '" + k.string +
                                           "' (expected one of: " + names_of(kBuildingKinds) + ")");
              continue;
            }
            aura.kinds |= 1u << static_cast<uint32_t>(kind);
          }
        }
      }
      b.auras.push_back(aura);
    }
  }

  // M2.2 "paths": [{ "id", "name", "tiers": [{ "name", "cost", "mods":
  // [{ "stat", "op": "add" | "mul", "value" }] }] }]. Optional; at most
  // kUpgradePaths paths of 1..kMaxUpgradeTiers tiers. "arc" + "add" is in degrees.
  void read_paths(const Obj& o, BuildingDef& b) {
    const Value* paths = o.array("paths");
    if (paths == nullptr) return;
    if (paths->array.size() > static_cast<size_t>(kUpgradePaths)) {
      o.field_error("paths", "at most " + std::to_string(kUpgradePaths) + " upgrade paths, got " +
                                 std::to_string(paths->array.size()));
      return;
    }
    for (size_t i = 0; i < paths->array.size(); ++i) {
      const Obj po = o.element("paths", *paths, i);
      if (!po.valid()) continue;
      UpgradePathDef path;
      po.str("id", path.id);
      path.name = path.id;
      if (po.has("name")) po.str("name", path.name);
      for (const UpgradePathDef& other : b.paths)
        if (!path.id.empty() && other.id == path.id)
          po.field_error("id", "duplicate path id '" + path.id + "'");
      if (const Value* tiers = po.array("tiers")) {
        if (tiers->array.empty() || tiers->array.size() > static_cast<size_t>(kMaxUpgradeTiers))
          po.field_error("tiers", "a path has 1 to " + std::to_string(kMaxUpgradeTiers) +
                                      " tiers, got " + std::to_string(tiers->array.size()));
        for (size_t t = 0; t < tiers->array.size() && t < static_cast<size_t>(kMaxUpgradeTiers); ++t) {
          const Obj to = po.element("tiers", *tiers, t);
          UpgradeTierDef tier;
          if (to.valid()) {
            tier.name = "Tier " + std::to_string(t + 1);
            if (to.has("name")) to.str("name", tier.name);
            to.num("cost", tier.cost, kNonNeg);
            if (const Value* mods = to.array("mods")) {
              if (mods->array.empty()) to.field_error("mods", "a tier needs at least one modifier");
              for (size_t m = 0; m < mods->array.size(); ++m) {
                const Obj mo = to.element("mods", *mods, m);
                if (mo.valid()) read_modifier(mo, b, tier);
              }
            }
          }
          path.tiers.push_back(std::move(tier));  // kept even when invalid, so tier numbers stay aligned
        }
      }
      b.paths.push_back(std::move(path));
    }
  }

  void read_modifier(const Obj& mo, const BuildingDef& b, UpgradeTierDef& tier) {
    StatModifier mod;
    const bool statOk = mo.named("stat", kUpgradeStats, mod.stat);
    const bool opOk = mo.named("op", kModOps, mod.op);
    double value = 0.0;
    if (!statOk || !opOk) return;
    const bool forPanel = mod.stat == UpgradeStat::Income;
    if (forPanel != (b.kind == BuildingKind::Panel)) {
      mo.field_error("stat", forPanel ? "'income' only applies to panels"
                                      : "only 'income' applies to panels");
      return;
    }
    // A multiplier of 0 or less would switch the stat off: not an upgrade.
    if (!mo.num("value", value, mod.op == ModOp::Mul ? kPositive : kAny)) return;
    mod.value = mod.stat == UpgradeStat::Arc && mod.op == ModOp::Add ? deg2rad(value)
                                                                      : static_cast<float>(value);
    tier.mods.push_back(mod);
  }

  // "falloff": [[t, mult], ...] with t in 0..1 ascending. Optional: flat 1.
  // The multiplier floor is checked in resolve(), once rules are known.
  void read_falloff(const Obj& o, BuildingDef& b) {
    if (!o.has("falloff")) {
      b.falloff = {{0.0f, 1.0f}, {1.0f, 1.0f}};
      return;
    }
    const Value* f = o.array("falloff");
    if (f == nullptr) return;
    if (f->array.empty()) o.field_error("falloff", "must not be empty");
    double prevT = 0.0;
    for (const Value& pt : f->array) {
      if (!pt.is_array() || pt.array.size() != 2 || !pt.array[0].is_number() ||
          !pt.array[1].is_number()) {
        o.field_error("falloff", "each point must be [t, multiplier]");
        continue;
      }
      const double t = pt.array[0].number, m = pt.array[1].number;
      if (!o.check_range("falloff", t, kUnit) || !o.check_range("falloff", m, kNonNeg)) continue;
      if (t < prevT) o.field_error("falloff", "points must be in ascending t order");
      prevT = t;
      b.falloff.push_back({static_cast<float>(t), static_cast<float>(m)});
    }
  }

  void read_enemy(const Obj& o, const std::string& stem) {
    EnemyDef e;
    if (!read_id(o, stem, db.enemyIds, db.enemies.size(), e.id)) return;
    if (o.has("behavior")) o.named("behavior", kEnemyBehaviors, e.behavior);
    o.named("shape", kShapes, e.shape);
    e.visual.shape = e.shape;
    read_visual(o, e.visual, false);
    o.integer("layers", e.layers, Range{1.0, false, 64.0});
    if (o.has("hpPerLayer")) o.num("hpPerLayer", e.hpPerLayer, kPositive);
    e.hp = e.hpPerLayer * static_cast<float>(e.layers);
    if (o.has("hp")) {
      o.num("hp", e.hp, kPositive);
      if (e.layers != 1) o.field_error("hp", "only allowed on single-layer enemies");
      e.hpPerLayer = e.hp;
    }
    o.num("speed", e.speed, kPositive);
    o.num("size", e.size, kPositive);
    if (o.has("landDamage")) {
      e.flatLandDamage = true;
      o.num("landDamage", e.landDamage, kNonNeg);
      if (o.has("landDamagePerLayer"))
        o.error("give either 'landDamage' or 'landDamagePerLayer', not both");
    } else if (o.has("landDamagePerLayer")) {
      o.num("landDamagePerLayer", e.landDamagePerLayer, kNonNeg);
    } else {
      o.error("missing field 'landDamagePerLayer' (or 'landDamage')");
    }
    o.num("bounty", e.bounty, kNonNeg);
    o.boolean("shadowCaster", e.shadowCaster);
    if (o.has("hpBar")) o.boolean("hpBar", e.hpBar);
    if (const Value* t = o.array("traits")) {
      for (const Value& tr : t->array) {
        uint32_t bit = 0;
        if (!tr.is_string() || !lookup_named(kTraits, tr.string, bit)) {
          o.field_error("traits", "unknown trait '" + tr.string + "'");
          continue;
        }
        e.traits |= bit;
      }
    }
    if (o.has("damageMult")) read_type_mults(o, "damageMult", e.damageMult);
    if (e.layers > 1) e.traits |= kTraitLayered;
    if (o.has("shield")) o.num("shield", e.shield, kPositive);
    if ((e.traits & kTraitShielded) && !(e.shield > 0.0f))
      o.field_error("traits", "'shielded' needs a 'shield' amount > 0");
    if (e.shield > 0.0f) e.traits |= kTraitShielded;
    if (o.has("hpBarSegments")) o.integer("hpBarSegments", e.hpBarSegments, Range{1.0, false, 64.0});
    PendingEnemy pe;
    pe.where = o.where();
    // M2.4: "weakPoint": {"mult": 3.5, "bodyMult": 0.25, "windowDeg": 8 (optional)}.
    if (o.has("weakPoint")) {
      const Obj w = o.object("weakPoint");
      if (w.valid()) {
        e.hasWeakPoint = true;
        w.num("mult", e.weakPoint.mult, Range{1.0, false, 10.0});
        w.num("bodyMult", e.weakPoint.bodyMult, Range{0.0, false, 1.0});
        if (w.has("windowDeg")) {
          pe.weakWindowSet = w.angle("windowDeg", e.weakPoint.window, Range{0.0, true, 45.0});
        }
      }
    }
    if ((e.traits & kTraitBulwark) && !e.hasWeakPoint)
      o.field_error("traits", "'bulwark' needs a 'weakPoint' object");
    if (e.hasWeakPoint) e.traits |= kTraitBulwark;
    if (e.behavior == EnemyBehavior::Eclipser) {
      const Obj h = o.object("hover");
      if (h.valid()) {
        e.hasHover = true;
        h.num("hoverRadius", e.hover.hoverRadius, kPositive);
        h.num("hoverSeconds", e.hover.hoverSeconds, kNonNeg);
        h.num("descendSpeed", e.hover.descendSpeed, kPositive);
        h.num("spawnRadius", e.hover.spawnRadius, kPositive);
        h.angle("spawnJitterDeg", e.hover.spawnJitter, Range{0.0, false, 180.0});
        if (e.hover.spawnRadius < e.hover.hoverRadius)
          h.field_error("spawnRadius", "must be >= hoverRadius");
      }
    }
    if (o.has("popsInto")) o.str("popsInto", pe.popsInto);
    pendingEnemies.push_back(std::move(pe));
    db.enemies.push_back(std::move(e));
  }

  void read_wave_set(const Obj& o, const std::string& stem) {
    WaveSetDef ws;
    if (!read_id(o, stem, db.waveSetIds, db.waveSets.size(), ws.id)) return;
    PendingWaveSet pw;
    pw.where = o.where();
    o.str("planet", pw.planet);
    o.str("sun", pw.sun);
    if (o.has("autoStartSeconds")) o.num("autoStartSeconds", ws.autoStartSeconds, kNonNeg);
    if (const Value* waves = o.array("waves")) {
      if (waves->array.empty()) o.field_error("waves", "must not be empty");
      for (size_t i = 0; i < waves->array.size(); ++i) {
        const Obj w = o.element("waves", *waves, i);
        if (!w.valid()) continue;
        WaveDef wave;
        std::vector<PendingSpawn> pending;
        if (w.integer("wave", wave.wave, Range{1.0, false, 100000.0}) &&
            wave.wave != static_cast<int>(ws.waves.size()) + 1) {
          w.field_error("wave", "waves must be numbered 1, 2, 3... in order; expected " +
                                    std::to_string(ws.waves.size() + 1) + ", got " +
                                    std::to_string(wave.wave));
        }
        w.num("lightMult", wave.lightMult, kPositive);
        w.num("spawnSeconds", wave.spawnSeconds, kPositive);
        const Obj fc = w.object("forecast");
        if (fc.valid()) {
          fc.integer("exactAhead", wave.forecast.exactAhead, Range{0.0, false, 1000.0});
          fc.integer("iconsAhead", wave.forecast.iconsAhead, Range{0.0, false, 1000.0});
          if (wave.forecast.iconsAhead < wave.forecast.exactAhead)
            fc.field_error("iconsAhead", "must be >= exactAhead");
        }
        if (const Value* spawns = w.array("spawns")) {
          if (spawns->array.empty()) w.field_error("spawns", "must not be empty");
          for (size_t j = 0; j < spawns->array.size(); ++j) {
            const Obj s = w.element("spawns", *spawns, j);
            if (!s.valid()) continue;
            SpawnDef spawn;
            PendingSpawn ps;
            s.str("lane", ps.lane);
            s.str("enemy", ps.enemy);
            s.integer("count", spawn.count, Range{1.0, false, 1000000.0});
            wave.spawns.push_back(spawn);
            pending.push_back(std::move(ps));
          }
        }
        ws.waves.push_back(std::move(wave));
        pw.spawns.push_back(std::move(pending));
      }
    }
    pendingWaveSets.push_back(std::move(pw));
    db.waveSets.push_back(std::move(ws));
  }

  void err(const std::string& where, const std::string& msg) { errors.push_back(where + ": " + msg); }

  void resolve() {
    const float floor = db.rules.minDamageMult;
    const std::string floorMsg = " is below the damage multiplier floor " + fmt(floor) +
                                 " (no hard counters)";

    for (size_t i = 0; i < db.buildings.size(); ++i) {
      for (const FalloffPoint& pt : db.buildings[i].falloff)
        if (pt.mult < floor)
          err(buildingWhere[i], "'falloff': multiplier " + fmt(pt.mult) + floorMsg);
    }

    // M2.3: trait names always resolve, with or without a damage.json.
    for (const auto& t : kTraits) {
      TraitDef& def = db.damage.traits[trait_bit_index(t.value)];
      def.id = t.name;
      if (def.name.empty()) def.name = t.name;
      for (const auto& dt : kDamageTypes) {
        const float m = def.mult[static_cast<int>(dt.value)];
        if (m < floor)
          err(damageWhere, std::string("traits: ") + t.name + ": mult: '" + dt.name +
                               "': multiplier " + fmt(m) + floorMsg);
      }
    }
    for (const auto& dt : kDamageTypes) {
      const float m = db.damage.shieldMult[static_cast<int>(dt.value)];
      if (m < floor)
        err(damageWhere, std::string("shieldMult: '") + dt.name + "': multiplier " + fmt(m) + floorMsg);
    }

    // M2.2: a tower must keep working at every tier combination.
    for (size_t i = 0; i < db.buildings.size(); ++i) {
      const BuildingDef& b = db.buildings[i];
      if (b.kind != BuildingKind::Tower || b.paths.empty()) continue;
      const size_t n0 = b.paths[0].tiers.size();
      const size_t n1 = b.paths.size() > 1 ? b.paths[1].tiers.size() : 0;
      bool bad = false;
      for (size_t t0 = 0; t0 <= n0 && !bad; ++t0) {
        for (size_t t1 = 0; t1 <= n1 && !bad; ++t1) {
          const uint8_t tiers[kUpgradePaths] = {static_cast<uint8_t>(t0), static_cast<uint8_t>(t1)};
          const BuildingStats s = effectiveStats(b, tiers);
          if (s.range > 0.0f && s.fireRate > 0.0f && s.arc > 0.0f) continue;
          err(buildingWhere[i], "'paths': range, fireRate and arc must stay above 0 (tiers " +
                                    std::to_string(t0) + "/" + std::to_string(t1) + ")");
          bad = true;
        }
      }
    }

    for (size_t i = 0; i < db.enemies.size(); ++i) {
      EnemyDef& e = db.enemies[i];
      const PendingEnemy& pe = pendingEnemies[i];
      for (const auto& dt : kDamageTypes) {
        const float m = e.damageMult[static_cast<int>(dt.value)];
        if (m < floor)
          err(pe.where, std::string("damageMult: '") + dt.name + "': multiplier " + fmt(m) + floorMsg);
      }
      if (e.hasWeakPoint) {  // M2.4
        if (!pe.weakWindowSet) e.weakPoint.window = db.rules.weakWindow;
        if (e.weakPoint.bodyMult < floor)
          err(pe.where, "'weakPoint': 'bodyMult' " + fmt(e.weakPoint.bodyMult) + floorMsg);
      }
      if (!pe.popsInto.empty()) {
        e.popsInto = db.enemy_index(pe.popsInto);
        if (e.popsInto == kNoIndex)
          err(pe.where, "'popsInto': unknown enemy id '" + pe.popsInto + "'");
      }
    }
    // Layer chains: no cycles, and each step down removes exactly one layer.
    for (size_t i = 0; i < db.enemies.size(); ++i) {
      const EnemyDef& e = db.enemies[i];
      const std::string& where = pendingEnemies[i].where;
      if (e.popsInto == kNoIndex) {
        if (e.layers > 1 && pendingEnemies[i].popsInto.empty())
          err(where, "missing field 'popsInto': an enemy with " + std::to_string(e.layers) +
                         " layers must name what it pops into");
        continue;
      }
      int cur = static_cast<int>(i);
      size_t steps = 0;
      while (cur != kNoIndex && steps <= db.enemies.size()) {
        cur = db.enemies[static_cast<size_t>(cur)].popsInto;
        ++steps;
      }
      if (cur != kNoIndex) {
        err(where, "'popsInto': chain loops back on itself");
        continue;
      }
      const EnemyDef& next = db.enemies[static_cast<size_t>(e.popsInto)];
      if (e.layers != next.layers + 1)
        err(where, "'layers': " + std::to_string(e.layers) + " but popsInto '" + next.id + "' has " +
                       std::to_string(next.layers) + " (must be exactly one fewer)");
    }

    for (size_t i = 0; i < db.waveSets.size(); ++i) {
      WaveSetDef& ws = db.waveSets[i];
      const PendingWaveSet& pw = pendingWaveSets[i];
      if (!pw.planet.empty()) {
        ws.planet = db.planet_index(pw.planet);
        if (ws.planet == kNoIndex) err(pw.where, "'planet': unknown planet id '" + pw.planet + "'");
      }
      if (!pw.sun.empty()) {
        ws.sun = db.sun_index(pw.sun);
        if (ws.sun == kNoIndex) err(pw.where, "'sun': unknown sun id '" + pw.sun + "'");
      }
      const PlanetDef* planet =
          ws.planet == kNoIndex ? nullptr : &db.planets[static_cast<size_t>(ws.planet)];
      for (size_t w = 0; w < ws.waves.size(); ++w) {
        for (size_t s = 0; s < ws.waves[w].spawns.size(); ++s) {
          SpawnDef& spawn = ws.waves[w].spawns[s];
          const PendingSpawn& ps = pw.spawns[w][s];
          const std::string where =
              pw.where + ": waves[" + std::to_string(w) + "]: spawns[" + std::to_string(s) + "]";
          if (!ps.enemy.empty()) {
            spawn.enemy = db.enemy_index(ps.enemy);
            if (spawn.enemy == kNoIndex) err(where, "'enemy': unknown enemy id '" + ps.enemy + "'");
          }
          if (!ps.lane.empty() && planet != nullptr) {
            spawn.lane = planet->lane_index(ps.lane);
            if (spawn.lane == kNoIndex)
              err(where, "'lane': unknown lane '" + ps.lane + "' on planet '" + planet->id + "'");
          }
        }
      }
    }

    if (!haveRules) errors.push_back("rules.json: missing file");
    if (db.planets.empty()) errors.push_back("planets/: no planet defined");
    if (db.suns.empty()) errors.push_back("sun/: no sun defined");
  }
};

// "res://content/enemies/dart.json" -> category "enemies", stem "dart",
// display "enemies/dart.json".
void split_path(const std::string& path, std::string& category, std::string& stem,
                std::string& display) {
  std::string p = path;
  for (char& c : p)
    if (c == '\\') c = '/';
  const size_t slash = p.find_last_of('/');
  const std::string file = slash == std::string::npos ? p : p.substr(slash + 1);
  const std::string dir = slash == std::string::npos ? std::string() : p.substr(0, slash);
  const size_t dslash = dir.find_last_of('/');
  category = dslash == std::string::npos ? dir : dir.substr(dslash + 1);
  const size_t dot = file.find_last_of('.');
  stem = dot == std::string::npos ? file : file.substr(0, dot);
  display = file == "rules.json" || file == "damage.json" ? file : category + "/" + file;
}

}  // namespace

const char* damage_type_name(DamageType type) {
  for (const auto& dt : kDamageTypes)
    if (dt.value == type) return dt.name;
  return "";
}

bool load_content_from_strings(const std::map<std::string, std::string>& files, ContentDB& out,
                               std::vector<std::string>& errors) {
  const size_t errorsBefore = errors.size();
  Loader loader(errors);
  for (const auto& kv : files) {
    std::string category, stem, display;
    split_path(kv.first, category, stem, display);
    Value doc;
    std::string perr;
    if (!mini_json::parse(kv.second, doc, perr)) {
      errors.push_back(display + ": JSON parse error: " + perr);
      continue;
    }
    if (!doc.is_object()) {
      errors.push_back(display + ": top level must be an object");
      continue;
    }
    const Obj o(&doc, display, errors);
    if (display == "rules.json") {
      if (loader.haveRules) o.error("more than one rules.json");
      else loader.read_rules(o);
    } else if (display == "damage.json") {
      if (loader.haveDamage) o.error("more than one damage.json");
      else loader.read_damage(o);
    } else if (category == "planets") {
      loader.read_planet(o, stem);
    } else if (category == "sun") {
      loader.read_sun(o, stem);
    } else if (category == "buildings") {
      loader.read_building(o, stem);
    } else if (category == "enemies") {
      loader.read_enemy(o, stem);
    } else if (category == "waves") {
      loader.read_wave_set(o, stem);
    } else {
      errors.push_back(kv.first + ": unknown content category '" + category + "'");
    }
  }
  loader.resolve();
  if (errors.size() != errorsBefore) return false;
  out = std::move(loader.db);
  return true;
}

}  // namespace orbitfall
