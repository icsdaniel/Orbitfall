#pragma once
// A small hand-built ContentDB for unit tests on tiny worlds. Its numbers are
// fixed here so the tests do not move when content/ is rebalanced.
#include "content/content.h"
#include "sim/mathx.h"
#include "sim/world.h"

namespace orbitfall_test {

inline constexpr float kDeg = orbitfall::kPi / 180.0f;

// Indices into tinyContent().
enum TinyBuilding { kGatling = 0, kCannon = 1, kPanel = 2 };
enum TinyEnemy { kRed = 0, kBlue = 1, kGreen = 2, kDart = 3, kEclipser = 4 };

inline orbitfall::ContentDB tinyContent() {
  using namespace orbitfall;
  ContentDB db;
  db.rules.killBounty = 1.0f;
  db.rules.minDamageMult = 0.25f;
  db.rules.spawnRadius = 900.0f;
  db.rules.spawnJitter = 0.0f;

  PlanetDef p;
  p.id = "tiny";
  p.radius = 100.0f;
  p.slots = 36;
  p.hp = 100.0f;
  p.rotMaxSpeed = 90.0f * kDeg;
  p.rotAccel = 360.0f * kDeg;
  p.rotDamping = 4.0f;
  p.startStardust = 150.0f;
  p.lanes = {{"A", 30.0f * kDeg}, {"B", 170.0f * kDeg}};
  p.surfaceTimeScale = 0.75f;
  p.sellRefund = 0.7f;
  db.planets.push_back(p);

  SunDef s;
  s.id = "sun";
  s.type = SunPathType::ConstantDrift;
  s.start = 0.0f;
  s.speed = 1.5f * kDeg;
  s.beamWidth = 141.0f;
  s.baseStrength = 1.0f;
  db.suns.push_back(s);

  BuildingDef g;
  g.id = "gatling";
  g.kind = BuildingKind::Tower;
  g.cost = 50.0f;
  g.aimMount = true;
  g.arc = 40.0f * kDeg;
  g.range = 350.0f;
  g.fireRate = 6.0f;
  g.hasProjectile = true;
  g.projectile = {ProjectileKind::Dot, 900.0f, 1.0f, DamageType::Kinetic, 0.0f};
  g.falloff = {{0.0f, 1.0f}, {1.0f, 1.0f}};
  g.targeting = {TargetingMode::First, TargetingMode::Strong, TargetingMode::Close};
  db.buildings.push_back(g);

  BuildingDef c = g;
  c.id = "cannon";
  c.cost = 90.0f;
  c.arc = 25.0f * kDeg;
  c.range = 450.0f;
  c.fireRate = 0.8f;
  c.projectile = {ProjectileKind::Shell, 600.0f, 3.0f, DamageType::Explosive, 40.0f};
  db.buildings.push_back(c);

  BuildingDef panel;
  panel.id = "panel";
  panel.kind = BuildingKind::Panel;
  panel.cost = 40.0f;
  panel.panelRate = 3.0f;
  panel.visual.shape = Shape::Diamond;
  db.buildings.push_back(panel);

  EnemyDef red;
  red.id = "red";
  red.layers = 1;
  red.hpPerLayer = 1.0f;
  red.hp = 1.0f;
  red.speed = 40.0f;
  red.size = 8.0f;
  red.landDamagePerLayer = 1.0f;
  red.bounty = 1.0f;
  db.enemies.push_back(red);

  EnemyDef blue = red;
  blue.id = "blue";
  blue.layers = 2;
  blue.hp = 2.0f;
  blue.popsInto = kRed;
  db.enemies.push_back(blue);

  EnemyDef green = red;
  green.id = "green";
  green.layers = 3;
  green.hp = 3.0f;
  green.popsInto = kBlue;
  db.enemies.push_back(green);

  EnemyDef dart = red;
  dart.id = "dart";
  dart.shape = Shape::Triangle;
  dart.speed = 110.0f;
  dart.size = 6.0f;
  dart.traits = kTraitFast;
  db.enemies.push_back(dart);

  EnemyDef ecl;
  ecl.id = "eclipser";
  ecl.behavior = EnemyBehavior::Eclipser;
  ecl.shape = Shape::Disc;
  ecl.layers = 1;
  ecl.hpPerLayer = 40.0f;
  ecl.hp = 40.0f;
  ecl.speed = 25.0f;
  ecl.size = 28.0f;
  ecl.flatLandDamage = true;
  ecl.landDamage = 15.0f;
  ecl.bounty = 15.0f;
  ecl.shadowCaster = true;
  ecl.hpBar = true;
  ecl.hasHover = true;
  ecl.hover = {260.0f, 15.0f, 10.0f, 900.0f, 6.0f * kDeg};
  db.enemies.push_back(ecl);

  for (size_t i = 0; i < db.planets.size(); ++i) db.planetIds[db.planets[i].id] = static_cast<int>(i);
  for (size_t i = 0; i < db.suns.size(); ++i) db.sunIds[db.suns[i].id] = static_cast<int>(i);
  for (size_t i = 0; i < db.buildings.size(); ++i) db.buildingIds[db.buildings[i].id] = static_cast<int>(i);
  for (size_t i = 0; i < db.enemies.size(); ++i) db.enemyIds[db.enemies[i].id] = static_cast<int>(i);
  return db;
}

// Config for a sandbox run (no wave set) on `db`.
inline orbitfall::SimConfig sandboxConfig(const orbitfall::ContentDB& db, uint32_t seed = 1) {
  orbitfall::SimConfig cfg;
  cfg.seed = seed;
  cfg.content = &db;
  cfg.waveSet = orbitfall::kNoIndex;
  cfg.enemyCapacity = 2048;
  cfg.projectileCapacity = 2048;
  return cfg;
}

}  // namespace orbitfall_test
