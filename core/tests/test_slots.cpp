// M1.3 acceptance (sim part): slot ring, no overlap, cost and 70% refund,
// rejection events.
#include <vector>

#include "doctest/doctest.h"
#include "sim/sim.h"
#include "tiny_content.h"

using namespace orbitfall;
using namespace orbitfall_test;

namespace {

// Events of one type since the last drain; the queue is left untouched.
std::vector<Event> eventsOf(const Sim& sim, EventType type) {
  std::vector<Event> out;
  for (const Event& e : sim.world().events.items)
    if (e.type == type) out.push_back(e);
  return out;
}

}  // namespace

TEST_CASE("slots: the ring has one empty entry per planet slot and the start stardust") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  const World& w = sim.world();
  REQUIRE(w.slotCount() == 36);
  for (const BuildingSlot& b : w.slots) CHECK(b.type == -1);
  CHECK(w.stardust == 150.0f);
  CHECK(w.slotAngle(9) == doctest::Approx(kPi / 2.0f));
}

TEST_CASE("slots: placing pays the cost and fills the slot") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 3, kCannon, 30.0f * kDeg));
  sim.step();
  const World& w = sim.world();
  CHECK(w.slots[3].type == kCannon);
  CHECK(w.slots[3].aimOffset == doctest::Approx(30.0f * kDeg));
  CHECK(w.slots[3].mode == static_cast<uint8_t>(TargetingMode::First));
  CHECK(w.stardust == 60.0f);
  const auto placed = eventsOf(sim, EventType::BuildingPlaced);
  REQUIRE(placed.size() == 1);
  CHECK(placed[0].a == 3);
  CHECK(placed[0].b == kCannon);
  CHECK(placed[0].value == 90.0f);
  const auto dust = eventsOf(sim, EventType::StardustChanged);
  REQUIRE(dust.size() == 1);
  CHECK(dust[0].value == 60.0f);
}

TEST_CASE("slots: no overlap, the second building on a slot is rejected and not charged") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 5, kGatling, 0.0f));
  sim.push(cmd::placeBuilding(0, 5, kPanel, 0.0f));
  sim.step();
  CHECK(sim.world().slots[5].type == kGatling);
  CHECK(sim.world().stardust == 100.0f);
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].a == 5);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::Occupied));
}

TEST_CASE("slots: unaffordable, bad slot and bad building are rejected") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kCannon, 0.0f));  // 150 -> 60
  sim.push(cmd::placeBuilding(0, 1, kCannon, 0.0f));  // needs 90
  sim.push(cmd::placeBuilding(0, 36, kPanel, 0.0f));
  sim.push(cmd::placeBuilding(0, -1, kPanel, 0.0f));
  sim.push(cmd::placeBuilding(0, 2, 99, 0.0f));
  sim.step();
  const World& w = sim.world();
  CHECK(w.stardust == 60.0f);
  CHECK(w.slots[1].type == -1);
  CHECK(w.slots[2].type == -1);
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 4);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::Unaffordable));
  CHECK(rejected[0].value == 90.0f);
  CHECK(rejected[1].b == static_cast<int32_t>(RejectReason::InvalidSlot));
  CHECK(rejected[2].b == static_cast<int32_t>(RejectReason::InvalidSlot));
  CHECK(rejected[3].b == static_cast<int32_t>(RejectReason::InvalidBuilding));
}

TEST_CASE("slots: selling refunds sellRefund of the cost and frees the slot") {
  ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 7, kCannon, 0.0f));
  sim.push(cmd::sellBuilding(1, 7));
  sim.push(cmd::sellBuilding(2, 7));  // already empty
  for (int i = 0; i < 3; ++i) sim.step();
  CHECK(sim.world().slots[7].type == -1);
  CHECK(sim.world().stardust == doctest::Approx(150.0f - 90.0f + 63.0f));
  const auto sold = eventsOf(sim, EventType::BuildingSold);
  REQUIRE(sold.size() == 1);
  CHECK(sold[0].value == doctest::Approx(63.0f));
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::EmptySlot));

  // The refund fraction is content, not code.
  db.planets[0].sellRefund = 0.5f;
  Sim half(sandboxConfig(db));
  half.push(cmd::placeBuilding(0, 0, kPanel, 0.0f));
  half.push(cmd::sellBuilding(0, 0));
  half.step();
  CHECK(half.world().stardust == doctest::Approx(150.0f - 40.0f + 20.0f));
  // The freed slot can be built on again.
  half.push(cmd::placeBuilding(1, 0, kGatling, 0.0f));
  half.step();
  CHECK(half.world().slots[0].type == kGatling);
}

TEST_CASE("slots: aim offset is clamped to +-60 degrees and ignored without a mount") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kGatling, 2.0f));
  sim.push(cmd::placeBuilding(0, 1, kPanel, 0.5f));
  sim.step();
  CHECK(sim.world().slots[0].aimOffset == doctest::Approx(60.0f * kDeg));
  CHECK(sim.world().slots[1].aimOffset == 0.0f);
  sim.push(cmd::setAim(1, 0, -30.0f * kDeg));
  sim.push(cmd::setAim(1, 1, 0.3f));  // panel: no mount
  sim.push(cmd::setAim(1, 2, 0.3f));  // empty
  sim.step();
  CHECK(sim.world().slots[0].aimOffset == doctest::Approx(-30.0f * kDeg));
  const auto rejected = eventsOf(sim, EventType::CommandRejected);
  REQUIRE(rejected.size() == 2);
  CHECK(rejected[0].b == static_cast<int32_t>(RejectReason::NotAllowed));
  CHECK(rejected[1].b == static_cast<int32_t>(RejectReason::EmptySlot));
}

TEST_CASE("slots: targeting mode must be one the building lists") {
  const ContentDB db = tinyContent();
  Sim sim(sandboxConfig(db));
  sim.push(cmd::placeBuilding(0, 0, kGatling, 0.0f));
  sim.push(cmd::placeBuilding(0, 1, kPanel, 0.0f));
  sim.push(cmd::setTargeting(0, 0, static_cast<uint8_t>(TargetingMode::Close)));
  sim.push(cmd::setTargeting(0, 0, 9));
  sim.push(cmd::setTargeting(0, 1, 0));
  sim.step();
  CHECK(sim.world().slots[0].mode == static_cast<uint8_t>(TargetingMode::Close));
  CHECK(eventsOf(sim, EventType::CommandRejected).size() == 2);
}

TEST_CASE("slots: buildings are part of the state hash") {
  const ContentDB db = tinyContent();
  Sim a(sandboxConfig(db)), b(sandboxConfig(db));
  a.push(cmd::placeBuilding(0, 4, kGatling, 0.0f));
  b.push(cmd::placeBuilding(0, 4, kGatling, 0.0f));
  a.step();
  b.step();
  CHECK(a.hash() == b.hash());
  b.world().slots[4].aimOffset = 0.1f;
  CHECK(a.hash() != b.hash());
}
