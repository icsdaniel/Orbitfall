#include "systems/buildings.h"

#include "sim/mathx.h"
#include "sim/world.h"

namespace orbitfall {
namespace {

bool reject(World& world, CommandType type, int32_t slot, RejectReason reason, float cost = 0.0f) {
  world.emit(EventType::CommandRejected, slot, static_cast<int32_t>(reason),
             static_cast<float>(static_cast<int>(type)), 0.0f, cost);
  return false;
}

bool isFinite(float v) { return v - v == 0.0f; }

float clampAim(float offset) {
  const float limit = kMaxAimOffsetDeg * kPi / 180.0f;
  return isFinite(offset) ? clampf(offset, -limit, limit) : 0.0f;
}

// Shared checks for commands that act on an existing building. Returns the
// slot's definition or nullptr after emitting the rejection.
const BuildingDef* occupied(World& world, CommandType type, int32_t slot) {
  if (slot < 0 || slot >= static_cast<int32_t>(world.slots.size())) {
    reject(world, type, slot, RejectReason::InvalidSlot);
    return nullptr;
  }
  const BuildingSlot& b = world.slots[static_cast<size_t>(slot)];
  if (b.type < 0) {
    reject(world, type, slot, RejectReason::EmptySlot);
    return nullptr;
  }
  return &world.content->buildings[static_cast<size_t>(b.type)];
}

}  // namespace

void addStardust(World& world, float delta) {
  world.stardust += delta;
  world.emit(EventType::StardustChanged, 0, 0, delta, 0.0f, world.stardust);
}

bool placeBuilding(World& world, int32_t slot, int32_t building, float aimOffset) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const CommandType type = CommandType::PlaceBuilding;
  if (slot < 0 || slot >= static_cast<int32_t>(world.slots.size())) {
    return reject(world, type, slot, RejectReason::InvalidSlot);
  }
  if (building < 0 || building >= static_cast<int32_t>(world.content->buildings.size())) {
    return reject(world, type, slot, RejectReason::InvalidBuilding);
  }
  BuildingSlot& b = world.slots[static_cast<size_t>(slot)];
  if (b.type >= 0) return reject(world, type, slot, RejectReason::Occupied);
  const BuildingDef& def = world.content->buildings[static_cast<size_t>(building)];
  if (world.stardust < def.cost) return reject(world, type, slot, RejectReason::Unaffordable, def.cost);

  b = BuildingSlot();
  b.type = static_cast<int16_t>(building);
  b.mode = def.targeting.empty() ? uint8_t(0) : static_cast<uint8_t>(def.targeting.front());
  b.aimOffset = def.aimMount ? clampAim(def.aimOffset + aimOffset) : 0.0f;
  addStardust(world, -def.cost);
  world.emit(EventType::BuildingPlaced, slot, building, 0.0f, 0.0f, def.cost);
  return true;
}

bool sellBuilding(World& world, int32_t slot) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const BuildingDef* def = occupied(world, CommandType::SellBuilding, slot);
  if (def == nullptr) return false;
  BuildingSlot& b = world.slots[static_cast<size_t>(slot)];
  const int32_t building = b.type;
  const float refund = def->cost * world.planet->sellRefund;
  b = BuildingSlot();
  addStardust(world, refund);
  world.emit(EventType::BuildingSold, slot, building, 0.0f, 0.0f, refund);
  return true;
}

bool setAim(World& world, int32_t slot, float aimOffset) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const BuildingDef* def = occupied(world, CommandType::SetAim, slot);
  if (def == nullptr) return false;
  if (!def->aimMount) return reject(world, CommandType::SetAim, slot, RejectReason::NotAllowed);
  world.slots[static_cast<size_t>(slot)].aimOffset = clampAim(aimOffset);
  return true;
}

bool setTargeting(World& world, int32_t slot, uint8_t mode) {
  if (world.content == nullptr || world.planet == nullptr) return false;
  const BuildingDef* def = occupied(world, CommandType::SetTargeting, slot);
  if (def == nullptr) return false;
  for (const TargetingMode m : def->targeting) {
    if (static_cast<uint8_t>(m) == mode) {
      world.slots[static_cast<size_t>(slot)].mode = mode;
      return true;
    }
  }
  return reject(world, CommandType::SetTargeting, slot, RejectReason::NotAllowed);
}

}  // namespace orbitfall
