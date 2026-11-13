#pragma once
// The game's systems, each `void name(World&, float dt)`, registered in tick
// order in core/sim/pipeline.cpp. Every system is a no-op on a world without
// content, so the bare M0 sim keeps working.

namespace orbitfall {

struct World;

// M1.1: turns the planet from world.rotateInput using the PlanetDef numbers.
void planetRotation(World& world, float dt);

}  // namespace orbitfall
