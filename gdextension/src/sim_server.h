#pragma once
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {
// Bridge between Godot and the C++ sim core. M0.1: smoke values only; M0.5 adds the sim.
class SimServer : public Node {
  GDCLASS(SimServer, Node)
 protected:
  static void _bind_methods();
 public:
  String core_version() const;
  int core_answer() const;
};
}  // namespace godot
