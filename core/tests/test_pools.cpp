#include <cstdint>
#include <vector>

#include "alloc_counter.h"
#include "doctest/doctest.h"
#include "pools/pool.h"
#include "sim/sim.h"

using namespace orbitfall;
using orbitfall_test::allocationCount;

namespace {

// The field set named in the task, declared through the public macro.
#define TEST_POOL_FIELDS(F) \
  F(float, posX)            \
  F(float, posY)            \
  F(float, velX)            \
  F(float, velY)            \
  F(float, hp)              \
  F(uint16_t, type)         \
  F(uint32_t, traits)       \
  F(uint8_t, flags)
ORBITFALL_DECLARE_POOL(TestPool, TEST_POOL_FIELDS)

// Writes values derived from `id` into every field of entity `i`.
void fill(TestPool& p, uint32_t i, uint32_t id) {
  p.posX[i] = static_cast<float>(id);
  p.posY[i] = static_cast<float>(id) * 2.0f;
  p.velX[i] = static_cast<float>(id) + 0.5f;
  p.velY[i] = -static_cast<float>(id);
  p.hp[i] = static_cast<float>(id % 7);
  p.type[i] = static_cast<uint16_t>(id);
  p.traits[i] = id * 2654435761u;
  p.flags[i] = static_cast<uint8_t>(id & 0xFF);
}

// True when every field of entity `i` still belongs to `id`.
bool matches(const TestPool& p, uint32_t i, uint32_t id) {
  return p.posX[i] == static_cast<float>(id) && p.posY[i] == static_cast<float>(id) * 2.0f &&
         p.velX[i] == static_cast<float>(id) + 0.5f && p.velY[i] == -static_cast<float>(id) &&
         p.hp[i] == static_cast<float>(id % 7) && p.type[i] == static_cast<uint16_t>(id) &&
         p.traits[i] == id * 2654435761u && p.flags[i] == static_cast<uint8_t>(id & 0xFF);
}

uint64_t hashOf(const TestPool& p) {
  Hasher h;
  p.hashInto(h);
  return h.state;
}

}  // namespace

TEST_CASE("pool: starts empty; reserve fixes the capacity and aligns every field") {
  TestPool p;
  CHECK(p.count == 0);
  CHECK(p.capacity == 0);
  CHECK(p.add() == kInvalidIndex);  // not reserved yet: refused, no crash
  CHECK(TestPool::kFieldCount == 8);

  REQUIRE(p.reserve(100));
  CHECK(p.capacity == 100);
  CHECK(p.count == 0);
  CHECK(p.overflowCount == 0);
  CHECK(reinterpret_cast<uintptr_t>(p.posX) % 64 == 0);
  CHECK(reinterpret_cast<uintptr_t>(p.hp) % 64 == 0);
  CHECK(reinterpret_cast<uintptr_t>(p.type) % 64 == 0);
  CHECK(reinterpret_cast<uintptr_t>(p.flags) % 64 == 0);
  // Arrays do not overlap.
  CHECK(reinterpret_cast<uintptr_t>(p.posY) >= reinterpret_cast<uintptr_t>(p.posX + 100));
  CHECK(reinterpret_cast<uintptr_t>(p.flags) >= reinterpret_cast<uintptr_t>(p.traits + 100));
}

TEST_CASE("pool: add returns consecutive indices with zeroed fields; iteration sees all") {
  TestPool p;
  REQUIRE(p.reserve(16));
  for (uint32_t id = 0; id < 10; ++id) {
    const uint32_t i = p.add();
    REQUIRE(i == id);
    CHECK(p.posX[i] == 0.0f);
    CHECK(p.hp[i] == 0.0f);
    CHECK(p.type[i] == 0);
    CHECK(p.traits[i] == 0u);
    CHECK(p.flags[i] == 0);
    fill(p, i, id);
  }
  CHECK(p.count == 10);

  uint32_t visited = 0;
  float sum = 0.0f;
  for (uint32_t i = 0; i < p.count; ++i) {
    CHECK(matches(p, i, i));
    sum += p.posX[i];
    ++visited;
  }
  CHECK(visited == 10);
  CHECK(sum == 45.0f);

  // A slot that held data is zeroed again when it is reused.
  p.remove(9);
  const uint32_t again = p.add();
  CHECK(again == 9);
  CHECK(p.posX[again] == 0.0f);
  CHECK(p.traits[again] == 0u);

  p.clear();
  CHECK(p.count == 0);
  CHECK(p.capacity == 16);
}

TEST_CASE("pool: swap-remove moves the last entity into the hole, all fields together") {
  TestPool p;
  REQUIRE(p.reserve(8));
  for (uint32_t id = 0; id < 5; ++id) fill(p, p.add(), id);

  SUBCASE("remove from the middle") {
    CHECK(p.remove(1));
    CHECK(p.count == 4);
    CHECK(matches(p, 0, 0));
    CHECK(matches(p, 1, 4));  // the old last one
    CHECK(matches(p, 2, 2));
    CHECK(matches(p, 3, 3));
  }
  SUBCASE("remove the last") {
    CHECK(p.remove(4));
    CHECK(p.count == 4);
    for (uint32_t i = 0; i < p.count; ++i) CHECK(matches(p, i, i));
  }
  SUBCASE("remove the first, then down to empty") {
    CHECK(p.remove(0));
    CHECK(matches(p, 0, 4));
    while (p.count > 0) CHECK(p.remove(0));
    CHECK(p.count == 0);
    CHECK(p.remove(0) == false);
  }
  SUBCASE("out-of-range remove does nothing") {
    CHECK(p.remove(5) == false);
    CHECK(p.remove(kInvalidIndex) == false);
    CHECK(p.count == 5);
    for (uint32_t i = 0; i < p.count; ++i) CHECK(matches(p, i, i));
  }
}

TEST_CASE("pool: random add/remove churn stays consistent with a reference model") {
  TestPool p;
  REQUIRE(p.reserve(64));
  std::vector<uint32_t> model;  // model[i] = id stored at pool index i
  Rng rng;
  rng.seed(2024);
  uint32_t nextId = 1;
  for (int step = 0; step < 20000; ++step) {
    if (rng.nextBelow(100) < 55) {
      const uint32_t i = p.add();
      if (model.size() < 64) {
        REQUIRE(i == model.size());
        fill(p, i, nextId);
        model.push_back(nextId++);
      } else {
        REQUIRE(i == kInvalidIndex);
      }
    } else if (!model.empty()) {
      const uint32_t i = rng.nextBelow(static_cast<uint32_t>(model.size()));
      REQUIRE(p.remove(i));
      model[i] = model.back();
      model.pop_back();
    }
    REQUIRE(p.count == model.size());
  }
  for (uint32_t i = 0; i < p.count; ++i) CHECK(matches(p, i, model[i]));
}

TEST_CASE("pool: removing while iterating visits every entity exactly once") {
  TestPool p;
  REQUIRE(p.reserve(32));
  for (uint32_t id = 0; id < 20; ++id) fill(p, p.add(), id);
  uint32_t seenMask = 0;
  for (uint32_t i = 0; i < p.count;) {
    const uint32_t id = p.type[i];
    CHECK((seenMask & (1u << id)) == 0u);
    seenMask |= 1u << id;
    if (id % 3 == 0) {
      p.remove(i);  // do not advance: the swapped-in entity is checked next
    } else {
      ++i;
    }
  }
  CHECK(seenMask == (1u << 20) - 1u);
  CHECK(p.count == 13);
  for (uint32_t i = 0; i < p.count; ++i) CHECK(p.type[i] % 3 != 0);
}

TEST_CASE("pool: overflow returns the invalid index and leaves the data alone") {
  TestPool p;
  REQUIRE(p.reserve(4));
  for (uint32_t id = 0; id < 4; ++id) fill(p, p.add(), id);
  CHECK(p.full());
  for (int k = 0; k < 10; ++k) CHECK(p.add() == kInvalidIndex);
  CHECK(p.count == 4);
  CHECK(p.overflowCount == 10);
  for (uint32_t i = 0; i < p.count; ++i) CHECK(matches(p, i, i));

  // Space freed by a remove can be used again.
  CHECK(p.remove(0));
  CHECK(p.add() == 3);

  TestPool empty;
  REQUIRE(empty.reserve(0));
  CHECK(empty.add() == kInvalidIndex);
  CHECK(empty.remove(0) == false);
}

TEST_CASE("pool: reserve again empties the pool at the new capacity") {
  TestPool p;
  REQUIRE(p.reserve(4));
  fill(p, p.add(), 1);
  REQUIRE(p.reserve(1000));
  CHECK(p.count == 0);
  CHECK(p.capacity == 1000);
  for (uint32_t id = 0; id < 1000; ++id) fill(p, p.add(), id);
  for (uint32_t i = 0; i < p.count; ++i) REQUIRE(matches(p, i, i));
}

TEST_CASE("pool: hash covers count and live data only") {
  TestPool a, b;
  REQUIRE(a.reserve(8));
  REQUIRE(b.reserve(32));  // capacity is not part of the hash
  for (uint32_t id = 0; id < 3; ++id) {
    fill(a, a.add(), id);
    fill(b, b.add(), id);
  }
  CHECK(hashOf(a) == hashOf(b));

  b.hp[1] += 1.0f;
  CHECK(hashOf(a) != hashOf(b));
  b.hp[1] -= 1.0f;
  CHECK(hashOf(a) == hashOf(b));

  // Stale data past count does not matter.
  fill(b, b.add(), 99);
  CHECK(hashOf(a) != hashOf(b));
  b.remove(3);
  CHECK(hashOf(a) == hashOf(b));
}

TEST_CASE("pool: no allocation after reserve") {
  TestPool p;
  const uint64_t beforeReserve = allocationCount();
  const bool reserved = p.reserve(4096);
  const uint64_t afterReserve = allocationCount();
  REQUIRE(reserved);
  // The counter does see pool storage: one block for all fields.
  CHECK(afterReserve - beforeReserve == 1);

  Rng rng;
  rng.seed(7);
  uint64_t touched = 0;
  const uint64_t start = allocationCount();
  for (int round = 0; round < 50; ++round) {
    for (uint32_t k = 0; k < 5000; ++k) {  // past capacity on purpose
      const uint32_t i = p.add();
      if (i != kInvalidIndex) fill(p, i, k);
    }
    for (uint32_t i = 0; i < p.count; ++i) {
      p.posX[i] += p.velX[i];
      ++touched;
    }
    while (p.count > 100) p.remove(rng.nextBelow(p.count));
    Hasher h;
    p.hashInto(h);
    touched += h.state & 1u;
  }
  p.clear();
  const uint64_t end = allocationCount();
  CHECK(end - start == 0);
  CHECK(touched > 0);
  CHECK(p.overflowCount > 0);
}

namespace {

// Test system: spawns, moves and removes entities in the World pools using the world RNG.
void churnPools(World& w, float dt) {
  for (int k = 0; k < 3; ++k) {
    const uint32_t i = w.enemies.add();
    if (i == kInvalidIndex) break;
    w.enemies.posX[i] = w.rng.range(-900.0f, 900.0f);
    w.enemies.posY[i] = w.rng.range(-900.0f, 900.0f);
    w.enemies.velX[i] = w.rng.range(-40.0f, 40.0f);
    w.enemies.velY[i] = w.rng.range(-40.0f, 40.0f);
    w.enemies.hp[i] = static_cast<float>(1 + w.rng.nextBelow(3));
    w.enemies.type[i] = static_cast<uint16_t>(w.rng.nextBelow(4));
  }
  for (int k = 0; k < 5; ++k) {
    const uint32_t i = w.projectiles.add();
    if (i == kInvalidIndex) break;
    w.projectiles.velX[i] = w.rng.range(-600.0f, 600.0f);
    w.projectiles.velY[i] = w.rng.range(-600.0f, 600.0f);
    w.projectiles.damage[i] = 1.0f;
    w.projectiles.life[i] = w.rng.range(0.2f, 2.0f);
  }
  for (uint32_t i = 0; i < w.enemies.count; ++i) {
    w.enemies.posX[i] += w.enemies.velX[i] * dt;
    w.enemies.posY[i] += w.enemies.velY[i] * dt;
  }
  for (uint32_t i = 0; i < w.projectiles.count;) {
    w.projectiles.posX[i] += w.projectiles.velX[i] * dt;
    w.projectiles.posY[i] += w.projectiles.velY[i] * dt;
    w.projectiles.life[i] -= dt;
    if (w.projectiles.life[i] <= 0.0f) {
      w.projectiles.remove(i);
    } else {
      ++i;
    }
  }
  if (w.enemies.count > 0 && w.rng.nextBelow(2) == 0) {
    const uint32_t i = w.rng.nextBelow(w.enemies.count);
    w.emit(EventType::EnemyKilled, w.enemies.type[i], 0, w.enemies.posX[i], w.enemies.posY[i]);
    w.enemies.remove(i);
  }
}

SimConfig smallConfig(uint32_t seed) {
  SimConfig cfg;
  cfg.seed = seed;
  cfg.enemyCapacity = 512;
  cfg.projectileCapacity = 256;
  return cfg;
}

uint64_t runChurn(uint32_t seed, uint32_t ticks) {
  Sim sim(smallConfig(seed));
  sim.pipeline().add("churnPools", churnPools);
  sim.push(cmd::setTimeScale(500, 0.75f, 0.3f));
  sim.push(cmd::setTimeScale(900, 0.0f));
  sim.push(cmd::setTimeScale(960, 1.0f));
  for (uint32_t i = 0; i < ticks; ++i) {
    sim.step();
    sim.world().events.clear();
  }
  return sim.hash();
}

}  // namespace

TEST_CASE("world pools: capacities come from the config") {
  Sim sim(smallConfig(1));
  CHECK(sim.world().enemies.capacity == 512);
  CHECK(sim.world().projectiles.capacity == 256);
  CHECK(sim.world().enemies.count == 0);
  CHECK(sim.world().projectiles.count == 0);
}

TEST_CASE("world pools: entity data is part of the state hash") {
  Sim sim(smallConfig(1));
  World& w = sim.world();
  const uint64_t empty = sim.hash();

  const uint32_t e = w.enemies.add();
  const uint64_t oneEnemy = sim.hash();
  CHECK(oneEnemy != empty);
  w.enemies.hp[e] = 3.0f;
  CHECK(sim.hash() != oneEnemy);
  w.enemies.remove(e);
  CHECK(sim.hash() == empty);

  const uint32_t p = w.projectiles.add();
  const uint64_t oneProjectile = sim.hash();
  CHECK(oneProjectile != empty);
  CHECK(oneProjectile != oneEnemy);  // an enemy and a projectile are not interchangeable
  w.projectiles.posX[p] = 1.0f;
  CHECK(sim.hash() != oneProjectile);
}

TEST_CASE("world pools: 3,000 ticks of pool churn replay to the same hash") {
  const uint64_t a = runChurn(11, 3000);
  CHECK(a == runChurn(11, 3000));
  CHECK(a != runChurn(12, 3000));
}

TEST_CASE("world pools: stepping and restarting the sim does not allocate") {
  const SimConfig cfg = smallConfig(3);
  Sim sim(cfg);
  sim.pipeline().add("churnPools", churnPools);
  for (uint32_t t = 0; t < 600; t += 3) sim.push(cmd::rotateInput(t, 0.5f));  // within commandCapacity

  uint64_t overflow = 0;
  const uint64_t start = allocationCount();
  for (int i = 0; i < 600; ++i) {
    sim.step();
    sim.world().events.clear();
    if (i % 2 == 0) sim.push(cmd::rotateInput(sim.tick() + 1, -0.5f));
  }
  overflow = sim.world().enemies.overflowCount;
  sim.reset(cfg);  // same capacities: storage is reused
  for (int i = 0; i < 60; ++i) sim.advance(1.0 / 60.0);
  const uint64_t end = allocationCount();

  CHECK(end - start == 0);
  CHECK(overflow > 0);  // the enemy pool did fill up and refuse spawns
  CHECK(sim.world().enemies.count > 0);
}
