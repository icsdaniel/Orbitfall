// JSON -> ContentDB with validation. Exception-free (the GDExtension builds
// with -fno-exceptions). Every problem becomes one line in `errors`.
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "content/content_loader.h"
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
                                                  {"panel", BuildingKind::Panel}};
constexpr Named<BuildingLayer> kBuildingLayers[] = {{"ground", BuildingLayer::Ground},
                                                    {"elevated", BuildingLayer::Elevated}};
constexpr Named<ProjectileKind> kProjectileKinds[] = {{"dot", ProjectileKind::Dot},
                                                      {"shell", ProjectileKind::Shell}};
constexpr Named<DamageType> kDamageTypes[] = {
    {"kinetic", DamageType::Kinetic}, {"explosive", DamageType::Explosive},
    {"energy", DamageType::Energy},   {"cryo", DamageType::Cryo},
    {"emp", DamageType::Emp},         {"pierce", DamageType::Pierce}};
constexpr Named<TargetingMode> kTargetingModes[] = {{"first", TargetingMode::First},
                                                    {"strong", TargetingMode::Strong},
                                                    {"close", TargetingMode::Close}};
constexpr Named<SunPathType> kSunPathTypes[] = {{"constant_drift", SunPathType::ConstantDrift}};
constexpr Named<EnemyBehavior> kEnemyBehaviors[] = {{"lane", EnemyBehavior::Lane},
                                                    {"eclipser", EnemyBehavior::Eclipser}};
constexpr Named<uint32_t> kTraits[] = {
    {"camo", kTraitCamo},         {"armored", kTraitArmored},
    {"shielded", kTraitShielded}, {"fast", kTraitFast},
    {"regrow", kTraitRegrow},     {"splitter", kTraitSplitter},
    {"heat_resistant", kTraitHeatResistant}, {"burrower", kTraitBurrower},
    {"orbiter", kTraitOrbiter},   {"healer", kTraitHealer},
    {"bulwark", kTraitBulwark},   {"carrier", kTraitCarrier}};

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
    o.integer("footprint", b.footprint, Range{1.0, false, 3.0});
    o.named("layer", kBuildingLayers, b.layer);
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
    } else if (kindOk && b.kind == BuildingKind::Panel) {
      o.num("panelRate", b.panelRate, kNonNeg);
    }
    buildingWhere.push_back(o.where());
    db.buildings.push_back(std::move(b));
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
    if (o.has("damageMult")) {
      const Obj dm = o.object("damageMult");
      if (dm.valid()) {
        const Value* raw = o.get("damageMult", mini_json::Type::Object);
        for (const auto& kv : raw->object) {
          DamageType type = DamageType::Kinetic;
          if (!lookup_named(kDamageTypes, kv.first, type)) {
            dm.error("unknown damage type '" + kv.first + "'");
            continue;
          }
          // Floor checked in resolve(), once rules are known.
          dm.num(kv.first.c_str(), e.damageMult[static_cast<int>(type)], kNonNeg);
        }
      }
    }
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
    PendingEnemy pe;
    pe.where = o.where();
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

    for (size_t i = 0; i < db.enemies.size(); ++i) {
      EnemyDef& e = db.enemies[i];
      const PendingEnemy& pe = pendingEnemies[i];
      for (const auto& dt : kDamageTypes) {
        const float m = e.damageMult[static_cast<int>(dt.value)];
        if (m < floor)
          err(pe.where, std::string("damageMult: '") + dt.name + "': multiplier " + fmt(m) + floorMsg);
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
  display = file == "rules.json" ? file : category + "/" + file;
}

}  // namespace

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
