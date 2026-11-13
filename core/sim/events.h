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
  Count
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
