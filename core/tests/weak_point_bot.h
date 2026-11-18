#pragma once
// M2.4 rotate-to-weak-point bot: turns the planet, through the same rotate
// input a player has, so that a precision tower (the Lance) sits directly
// under the nearest weak-point enemy. It reads the world only through
// weakPointHint(), the same answer the client's aim assist shows.
// (M2.9 moves the bots to tools/sim-runner; this one is ready to move.)
#include <cmath>

#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/combat.h"

namespace orbitfall_bots {

struct WeakPointAimer {
  // Fraction of the stopping speed it aims for, so it brakes in time.
  float braking = 0.7f;

  // Rotate input in [-1, 1] for this tick (0 = let go).
  float input(const orbitfall::World& world) const {
    using namespace orbitfall;
    WeakPointHint hint;
    if (world.planet == nullptr || !weakPointHint(world, hint)) return 0.0f;
    const PlanetDef& p = *world.planet;
    const float dist = std::fabs(hint.turn);
    if (dist < 0.5f * kPi / 180.0f) return 0.0f;  // lined up: let it settle
    // Fastest speed from which it can still stop in `dist` at rotAccel.
    float speed = braking * std::sqrt(2.0f * p.rotAccel * dist);
    if (speed > p.rotMaxSpeed) speed = p.rotMaxSpeed;
    const float v = (hint.turn > 0.0f ? speed : -speed) / p.rotMaxSpeed;
    return clampf(v, -1.0f, 1.0f);
  }
};

}  // namespace orbitfall_bots
