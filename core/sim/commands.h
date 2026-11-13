#pragma once
// Commands in: every player action reaches the sim as a Command stamped with
// the tick it applies on. A run is fully described by seed + command log.
// Command is a flat POD so the GDExtension binding can marshal it directly.
#include <cstdint>
#include <type_traits>
#include <vector>

namespace orbitfall {

enum class CommandType : uint8_t {
  None = 0,
  RotateInput,    // value = input in [-1, 1]
  PlaceBuilding,  // slot, id = building index (content order), value = aimOffset (rad)
  SellBuilding,   // slot
  SetAim,         // slot, value = aimOffset (rad)
  SetTargeting,   // slot, mode = targeting mode index
  StartWave,      // no payload
  SetTimeScale,   // value = scale (0 = pause), value2 = ease seconds (0 = instant)
  Count
};

struct Command {
  uint32_t tick = 0;  // tick the command applies on; late commands apply on the next tick
  CommandType type = CommandType::None;
  uint8_t mode = 0;
  uint16_t reserved = 0;
  int32_t slot = -1;
  int32_t id = -1;
  float value = 0.0f;
  float value2 = 0.0f;
};
static_assert(std::is_trivially_copyable<Command>::value, "Command must stay a POD");
static_assert(sizeof(Command) == 24, "Command layout is part of the binding contract");

// Constructors for each command kind.
namespace cmd {
inline Command make(uint32_t tick, CommandType type) {
  Command c;
  c.tick = tick;
  c.type = type;
  return c;
}
inline Command rotateInput(uint32_t tick, float value) {
  Command c = make(tick, CommandType::RotateInput);
  c.value = value;
  return c;
}
inline Command placeBuilding(uint32_t tick, int32_t slot, int32_t buildingIndex, float aimOffset) {
  Command c = make(tick, CommandType::PlaceBuilding);
  c.slot = slot;
  c.id = buildingIndex;
  c.value = aimOffset;
  return c;
}
inline Command sellBuilding(uint32_t tick, int32_t slot) {
  Command c = make(tick, CommandType::SellBuilding);
  c.slot = slot;
  return c;
}
inline Command setAim(uint32_t tick, int32_t slot, float aimOffset) {
  Command c = make(tick, CommandType::SetAim);
  c.slot = slot;
  c.value = aimOffset;
  return c;
}
inline Command setTargeting(uint32_t tick, int32_t slot, uint8_t mode) {
  Command c = make(tick, CommandType::SetTargeting);
  c.slot = slot;
  c.mode = mode;
  return c;
}
inline Command startWave(uint32_t tick) { return make(tick, CommandType::StartWave); }
inline Command setTimeScale(uint32_t tick, float scale, float easeSeconds = 0.0f) {
  Command c = make(tick, CommandType::SetTimeScale);
  c.value = scale;
  c.value2 = easeSeconds;
  return c;
}
}  // namespace cmd

// Pending commands ordered by tick; commands with equal ticks keep push order.
// Not thread-safe: the binding hands commands over on the sim thread.
struct CommandQueue {
  std::vector<Command> items;  // items[head..] are pending, sorted by tick
  uint32_t head = 0;

  void reserve(uint32_t capacity) { items.reserve(capacity); }
  uint32_t pending() const { return static_cast<uint32_t>(items.size()) - head; }
  void push(const Command& c);
  // Pops the next command due at or before `tick` into `out`; false when none is due.
  bool popDue(uint32_t tick, Command& out);
  void clear() {
    items.clear();
    head = 0;
  }
};

}  // namespace orbitfall
