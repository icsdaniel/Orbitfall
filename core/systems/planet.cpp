// M1.1 planet rotation (BRIEF §4.1): the input in [-1, 1] accelerates the spin
// toward input * rotMaxSpeed with rotAccel; with no input the spin decays with
// rotDamping. All numbers come from the planet's content file.
#include "sim/mathx.h"
#include "sim/world.h"
#include "systems/systems.h"

namespace orbitfall {

void planetRotation(World& world, float dt) {
  const PlanetDef* planet = world.planet;
  if (planet == nullptr || dt <= 0.0f) return;

  float vel = world.planetAngVel;
  const float input = world.rotateInput;
  if (input != 0.0f) {
    const float target = input * planet->rotMaxSpeed;
    const float step = planet->rotAccel * dt;
    if (vel < target) {
      vel = vel + step > target ? target : vel + step;
    } else {
      vel = vel - step < target ? target : vel - step;
    }
  } else {
    // Released: exponential decay, in its exact-arithmetic linear form.
    const float keep = 1.0f - planet->rotDamping * dt;
    vel *= keep > 0.0f ? keep : 0.0f;
  }
  vel = clampf(vel, -planet->rotMaxSpeed, planet->rotMaxSpeed);
  world.planetAngVel = vel;
  world.planetRotation = wrapTau(world.planetRotation + vel * dt);
}

}  // namespace orbitfall
