// M2.3 acceptance on tiny worlds: the soft-counter matrix (trait rows x damage
// types), the 0.25 floor on every multiplier, and the Shielded absorb.
#include <cmath>

#include "doctest/doctest.h"
#include "sim/render_snapshot.h"
#include "sim/sim.h"
#include "systems/combat.h"
#include "systems/enemies.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

int bitIndex(uint32_t bit) {
  int i = 0;
  while ((1u << i) != bit) ++i;
  return i;
}

float& row(ContentDB& db, uint32_t trait, DamageType type) {
  return db.damage.traits[bitIndex(trait)].mult[static_cast<int>(type)];
}

// tinyContent() plus the PLAN §5.5 rows: Armored halves Kinetic,
// Heat-resistant halves Energy, EMP strips shields twice as fast.
ContentDB matrixContent() {
  ContentDB db = tinyContent();
  row(db, kTraitArmored, DamageType::Kinetic) = 0.5f;
  row(db, kTraitArmored, DamageType::Pierce) = 1.5f;
  row(db, kTraitHeatResistant, DamageType::Energy) = 0.5f;
  db.damage.shieldMult[static_cast<int>(DamageType::Emp)] = 2.0f;
  return db;
}

// A tower definition dealing `type`, flat falloff.
BuildingDef towerOf(const ContentDB& db, DamageType type) {
  BuildingDef b = db.buildings[kGatling];
  b.projectile.type = type;
  return b;
}

uint32_t putEnemy(World& w, int type, float x, float y) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  w.enemies.posX[i] = x;
  w.enemies.posY[i] = y;
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

}  // namespace

TEST_CASE("damage matrix: an enemy with no traits takes every type at 1x") {
  const ContentDB db = matrixContent();
  for (int t = 0; t < kDamageTypeCount; ++t)
    CHECK(typeMultiplier(db, db.enemies[kRed], static_cast<DamageType>(t)) == doctest::Approx(1.0f));
}

TEST_CASE("damage matrix: Armored halves Kinetic, Heat-resistant halves Energy") {
  const ContentDB db = matrixContent();
  EnemyDef armored = db.enemies[kRed];
  armored.traits = kTraitArmored;
  CHECK(typeMultiplier(db, armored, DamageType::Kinetic) == doctest::Approx(0.5f));
  CHECK(typeMultiplier(db, armored, DamageType::Explosive) == doctest::Approx(1.0f));
  CHECK(typeMultiplier(db, armored, DamageType::Pierce) == doctest::Approx(1.5f));

  EnemyDef heat = db.enemies[kRed];
  heat.traits = kTraitHeatResistant;
  CHECK(typeMultiplier(db, heat, DamageType::Energy) == doctest::Approx(0.5f));
  CHECK(typeMultiplier(db, heat, DamageType::Kinetic) == doctest::Approx(1.0f));

  // Rows multiply with each other and with the enemy's own damageMult.
  EnemyDef both = db.enemies[kRed];
  both.traits = kTraitArmored | kTraitHeatResistant | kTraitFast;
  both.damageMult[static_cast<int>(DamageType::Cryo)] = 2.0f;
  CHECK(typeMultiplier(db, both, DamageType::Kinetic) == doctest::Approx(0.5f));
  CHECK(typeMultiplier(db, both, DamageType::Energy) == doctest::Approx(0.5f));
  CHECK(typeMultiplier(db, both, DamageType::Cryo) == doctest::Approx(2.0f));
}

TEST_CASE("damage floor: no combination of rows, resistances and falloff goes below 0.25") {
  ContentDB db = matrixContent();
  // Deliberately illegal data (the loader would reject it): the code still floors.
  for (int bit = 0; bit < kTraitCount; ++bit)
    for (int t = 0; t < kDamageTypeCount; ++t) db.damage.traits[bit].mult[t] = 0.3f;
  for (int t = 0; t < kDamageTypeCount; ++t) db.damage.shieldMult[t] = 0.0f;
  EnemyDef e = db.enemies[kRed];
  for (int t = 0; t < kDamageTypeCount; ++t) e.damageMult[t] = 0.0f;
  for (uint32_t traits = 0; traits < (1u << kTraitCount); traits += 37) {
    e.traits = traits;
    for (int t = 0; t < kDamageTypeCount; ++t) {
      const DamageType type = static_cast<DamageType>(t);
      CHECK(typeMultiplier(db, e, type) >= 0.25f);
      CHECK(shieldMultiplier(db, type) >= 0.25f);
      BuildingDef tower = towerOf(db, type);
      tower.falloff = {{0.0f, 1.0f}, {1.0f, 0.0f}};
      for (float f = 0.0f; f <= 1.0f; f += 0.125f)
        CHECK(damageMultiplier(db, tower, e, f) >= 0.25f);
    }
  }
  // A floor raised in rules applies; one lowered there does not.
  db.rules.minDamageMult = 0.5f;
  CHECK(typeMultiplier(db, e, DamageType::Kinetic) == doctest::Approx(0.5f));
  db.rules.minDamageMult = 0.0f;
  CHECK(typeMultiplier(db, e, DamageType::Kinetic) == doctest::Approx(0.25f));
  CHECK(damageFloor(db) == doctest::Approx(kHardMinDamageMult));
}

TEST_CASE("damage matrix in a tiny world: a Kinetic tower needs twice the shots on Armored") {
  ContentDB db = matrixContent();
  EnemyDef tank = db.enemies[kRed];
  tank.id = "tank";
  tank.hp = tank.hpPerLayer = 6.0f;
  db.enemies.push_back(tank);
  const int plain = static_cast<int>(db.enemies.size()) - 1;
  tank.id = "tank_armored";
  tank.traits = kTraitArmored;
  db.enemies.push_back(tank);
  const int armored = plain + 1;

  // Gatling: 1 Kinetic damage per shot, 6 shots/s.
  int ticks[2] = {0, 0};
  const int types[2] = {plain, armored};
  for (int k = 0; k < 2; ++k) {
    Sim sim(sandboxConfig(db));
    sim.world().stardust = 1000.0f;
    sim.push(cmd::placeBuilding(sim.tick(), 0, kGatling, 0.0f));
    sim.step();
    putEnemy(sim.world(), types[k], 250.0f, 0.0f);
    while (sim.world().enemies.count > 0 && ticks[k] < 600) {
      sim.step();
      ++ticks[k];
    }
    REQUIRE(ticks[k] < 600);
  }
  // 6 shots vs 12 shots at 6 shots/s: about 1 s more.
  CHECK(ticks[1] > ticks[0]);
  const float ratio = static_cast<float>(ticks[1]) / static_cast<float>(ticks[0]);
  CHECK(ratio > 1.6f);
  CHECK(ratio < 2.4f);
}

TEST_CASE("shield: absorbs the first X damage, then the rest reaches the hit points") {
  ContentDB db = matrixContent();
  EnemyDef e = db.enemies[kRed];
  e.id = "bubble";
  e.hp = e.hpPerLayer = 4.0f;
  e.shield = 5.0f;
  e.traits = kTraitShielded;
  db.enemies.push_back(e);
  const int bubble = static_cast<int>(db.enemies.size()) - 1;

  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = putEnemy(w, bubble, 300.0f, 0.0f);
  CHECK(w.enemies.shield[i] == doctest::Approx(5.0f));
  CHECK((w.enemies.traits[i] & kTraitShielded) != 0);

  CHECK_FALSE(damageEnemy(w, i, 3.0f, DamageType::Kinetic));
  CHECK(w.enemies.shield[i] == doctest::Approx(2.0f));
  CHECK(w.enemies.hp[i] == doctest::Approx(4.0f));
  CHECK((w.enemies.traits[i] & kTraitShielded) != 0);

  // 3 more: 2 break the shield, 1 goes through. The glow-ring bit drops.
  CHECK_FALSE(damageEnemy(w, i, 3.0f, DamageType::Kinetic));
  CHECK(w.enemies.shield[i] == 0.0f);
  CHECK(w.enemies.hp[i] == doctest::Approx(3.0f));
  CHECK((w.enemies.traits[i] & kTraitShielded) == 0);

  CHECK(damageEnemy(w, i, 3.0f, DamageType::Kinetic));
}

TEST_CASE("shield: EMP strips it at the matrix rate, and the overflow is converted back") {
  ContentDB db = matrixContent();
  EnemyDef e = db.enemies[kRed];
  e.id = "bubble";
  e.hp = e.hpPerLayer = 4.0f;
  e.shield = 6.0f;
  e.traits = kTraitShielded;
  db.enemies.push_back(e);
  const int bubble = static_cast<int>(db.enemies.size()) - 1;

  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = putEnemy(w, bubble, 300.0f, 0.0f);
  damageEnemy(w, i, 2.0f, DamageType::Emp);  // 4 off the shield
  CHECK(w.enemies.shield[i] == doctest::Approx(2.0f));
  damageEnemy(w, i, 2.0f, DamageType::Emp);  // 1 damage breaks it, 1 goes through
  CHECK(w.enemies.shield[i] == 0.0f);
  CHECK(w.enemies.hp[i] == doctest::Approx(3.0f));
}

TEST_CASE("shield: carries over a popped layer and never returns once broken") {
  ContentDB db = matrixContent();
  EnemyDef inner = db.enemies[kRed];
  inner.id = "inner";
  inner.shield = 4.0f;
  inner.traits = kTraitShielded;
  db.enemies.push_back(inner);
  const int innerIdx = static_cast<int>(db.enemies.size()) - 1;
  EnemyDef outer = inner;
  outer.id = "outer";
  outer.layers = 2;
  outer.hp = 2.0f;
  outer.popsInto = innerIdx;
  outer.traits = kTraitShielded | kTraitLayered;
  db.enemies.push_back(outer);
  const int outerIdx = innerIdx + 1;

  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = putEnemy(w, outerIdx, 300.0f, 0.0f);
  // 4 shield + 1 layer: the inner layer does not get a fresh shield.
  CHECK_FALSE(damageEnemy(w, i, 5.0f, DamageType::Kinetic));
  CHECK(w.enemies.type[i] == innerIdx);
  CHECK(w.enemies.shield[i] == 0.0f);
  CHECK((w.enemies.traits[i] & kTraitShielded) == 0);
  CHECK(damageEnemy(w, i, 1.0f, DamageType::Kinetic));
}

TEST_CASE("render snapshot: trait bits follow the entity (layers popped, shield broken)") {
  ContentDB db = matrixContent();
  db.enemies[kBlue].traits = kTraitLayered | kTraitArmored;
  db.enemies[kBlue].shield = 2.0f;
  db.enemies[kBlue].traits |= kTraitShielded;
  db.enemies[kBlue].visual.colorTier = 1;
  db.enemies[kRed].visual.colorTier = 0;

  Sim sim(sandboxConfig(db));
  World& w = sim.world();
  const uint32_t i = putEnemy(w, kBlue, 300.0f, 0.0f);
  float buf[kRenderFloatsPerInstance];
  REQUIRE(fillEnemyRenderBuffer(w, buf, 1) == 1);
  CHECK(buf[13] == 1.0f);
  CHECK(buf[14] == static_cast<float>(kTraitLayered | kTraitArmored | kTraitShielded));

  damageEnemy(w, i, 2.0f, DamageType::Explosive);  // shield gone
  REQUIRE(fillEnemyRenderBuffer(w, buf, 1) == 1);
  CHECK(buf[14] == static_cast<float>(kTraitLayered | kTraitArmored));

  damageEnemy(w, i, 1.0f, DamageType::Explosive);  // layer popped: colour tier = health
  REQUIRE(fillEnemyRenderBuffer(w, buf, 1) == 1);
  CHECK(buf[13] == 0.0f);
  CHECK(buf[14] == 0.0f);
}
