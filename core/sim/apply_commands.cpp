#include "sim/hash.h"
#include "sim/mathx.h"
#include "sim/pipeline.h"
#include "sim/world.h"
#include "systems/buildings.h"
#include "systems/waves.h"

namespace orbitfall {
namespace {

bool isFinite(float v) { return v - v == 0.0f; }  // false for NaN and infinities

void digest(World& world, const Command& c) {
  Hasher h;
  h.add(world.commandDigest);
  h.add(world.tick);  // the tick it actually applied on
  h.add(c.type);
  h.add(c.mode);
  h.add(c.slot);
  h.add(c.id);
  h.add(c.value);
  h.add(c.value2);
  world.commandDigest = h.state;
  ++world.commandsApplied;
}

// Build commands are refused once the run has ended.
bool runOver(World& world, const Command& c) {
  if (world.phase != Phase::Won && world.phase != Phase::Lost) return false;
  if (world.content != nullptr) {
    world.emit(EventType::CommandRejected, c.slot, static_cast<int32_t>(RejectReason::WrongPhase),
               static_cast<float>(static_cast<int>(c.type)));
  }
  return true;
}

void apply(World& world, const Command& c) {
  switch (c.type) {
    case CommandType::RotateInput:
      if (isFinite(c.value)) world.rotateInput = clampf(c.value, -1.0f, 1.0f);
      break;
    case CommandType::SetTimeScale: {
      if (!isFinite(c.value)) break;
      const float ease = isFinite(c.value2) ? clampf(c.value2, 0.0f, 60.0f) : 0.0f;
      world.timeScaleFrom = world.timeScale;
      world.timeScaleTarget = clampf(c.value, 0.0f, kMaxTimeScale);
      world.easeTicksTotal = static_cast<uint32_t>(ease * static_cast<float>(kTickRate) + 0.5f);
      world.easeTicksLeft = world.easeTicksTotal;
      if (world.easeTicksTotal == 0) world.timeScale = world.timeScaleTarget;
      break;
    }
    case CommandType::PlaceBuilding:
      if (!runOver(world, c)) placeBuilding(world, c.slot, c.id, c.value);
      break;
    case CommandType::SellBuilding:
      if (!runOver(world, c)) sellBuilding(world, c.slot);
      break;
    case CommandType::SetAim:
      if (!runOver(world, c)) setAim(world, c.slot, c.value);
      break;
    case CommandType::SetTargeting:
      if (!runOver(world, c)) setTargeting(world, c.slot, c.mode);
      break;
    case CommandType::StartWave:
      if (world.content != nullptr && !startWave(world)) {
        world.emit(EventType::CommandRejected, -1, static_cast<int32_t>(RejectReason::WrongPhase),
                   static_cast<float>(static_cast<int>(c.type)));
      }
      break;
    case CommandType::None:
    case CommandType::Count:
      break;
  }
}

}  // namespace

void applyCommands(World& world, float /*dt*/) {
  Command c;
  while (world.commands.popDue(world.tick, c)) {
    apply(world, c);
    digest(world, c);
    if (world.recordCommands) {
      c.tick = world.tick;  // the tick it really applied on
      world.commandLog.push_back(c);
    }
  }

  // Ease the time scale in real ticks (so it also works from and to pause), smoothstep.
  if (world.easeTicksLeft > 0) {
    --world.easeTicksLeft;
    if (world.easeTicksLeft == 0) {
      world.timeScale = world.timeScaleTarget;
    } else {
      const float t = 1.0f - static_cast<float>(world.easeTicksLeft) /
                                 static_cast<float>(world.easeTicksTotal);
      const float s = t * t * (3.0f - 2.0f * t);
      world.timeScale = world.timeScaleFrom + (world.timeScaleTarget - world.timeScaleFrom) * s;
    }
  }
  world.dt = kFixedDt * world.timeScale;
}

void advanceClock(World& world, float dt) { world.simTime += static_cast<double>(dt); }

}  // namespace orbitfall
