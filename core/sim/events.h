#pragma once
// Events out: things the game layer reacts to (HUD, flow). Systems emit them
// through World::emit; the host drains the queue after each advance.
// Event is a flat POD so the GDExtension binding can marshal it directly.
#include <cstdint>
#include <type_traits>
#include <vector>

namespace orbitfall {

enum class EventType : uint8_t {
  None = 0,
  LayerPopped,
  EnemyKilled,
  PlanetHit,
  StardustChanged,
  WaveStarted,
  WaveCleared,
  ShadowChanged,
  GameOver,
  Victory,
  BuildingPlaced,   // a = slot, b = building index, value = cost paid
  BuildingSold,     // a = slot, b = building index, value = refund
  CommandRejected,  // a = slot, b = RejectReason, x = CommandType, value = cost (Unaffordable)
  Count
};

// Why a command was refused (Event::b of CommandRejected).
enum class RejectReason : int32_t {
  None = 0,
  InvalidSlot,      // slot outside the ring
  InvalidBuilding,  // unknown building index
  Occupied,         // PlaceBuilding on a used slot
  Unaffordable,     // not enough stardust
  EmptySlot,        // sell / aim / targeting on an empty slot
  NotAllowed,       // the building has no aim mount or lacks that targeting mode
  WrongPhase,       // not accepted in the current phase (e.g. after the run ended)
};

// Generic payload; each emitting system documents which fields it fills.
struct Event {
  uint32_t tick = 0;
  EventType type = EventType::None;
  uint8_t reserved[3] = {0, 0, 0};
  int32_t a = 0;  // e.g. slot, wave number, enemy type index
  int32_t b = 0;
  float x = 0.0f;  // world position, when the event has one
  float y = 0.0f;
  float value = 0.0f;  // e.g. damage, new stardust total
};
static_assert(std::is_trivially_copyable<Event>::value, "Event must stay a POD");
static_assert(sizeof(Event) == 28, "Event layout is part of the binding contract");

struct EventQueue {
  std::vector<Event> items;
  uint64_t totalEmitted = 0;  // never reset by clear(); part of the state hash

  void reserve(uint32_t capacity) { items.reserve(capacity); }
  void push(const Event& e) {
    items.push_back(e);
    ++totalEmitted;
  }
  uint32_t size() const { return static_cast<uint32_t>(items.size()); }
  // Host calls this after reading the events; capacity is kept.
  void clear() { items.clear(); }
};

}  // namespace orbitfall
