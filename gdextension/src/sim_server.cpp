#include "sim_server.h"
#include <godot_cpp/core/class_db.hpp>
#include "sim/version.h"

using namespace godot;

void SimServer::_bind_methods() {
  ClassDB::bind_method(D_METHOD("core_version"), &SimServer::core_version);
  ClassDB::bind_method(D_METHOD("core_answer"), &SimServer::core_answer);
}
String SimServer::core_version() const { return String(orbitfall::core_version()); }
int SimServer::core_answer() const { return orbitfall::core_answer(); }
