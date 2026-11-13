#include "sim/commands.h"

#include <cstddef>

namespace orbitfall {

void CommandQueue::push(const Command& c) {
  // Drop the consumed prefix once it dominates, so the vector does not creep.
  if (head >= 256 && static_cast<size_t>(head) * 2 >= items.size()) {
    items.erase(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(head));
    head = 0;
  }
  // Stable insert: after the last pending command with tick <= c.tick.
  size_t pos = items.size();
  while (pos > head && items[pos - 1].tick > c.tick) --pos;
  items.insert(items.begin() + static_cast<std::ptrdiff_t>(pos), c);
}

bool CommandQueue::popDue(uint32_t tick, Command& out) {
  if (head >= items.size() || items[head].tick > tick) return false;
  out = items[head++];
  if (head == items.size()) clear();
  return true;
}

}  // namespace orbitfall
