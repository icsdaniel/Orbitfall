#pragma once
// M1.7 sunlight: the pluggable SunPath, the beam test and lit-arc maths
// (BRIEF §4.2, §6).
#include "content/content.h"

namespace orbitfall {

// How the sun bearing moves. One entry per SunPathType; a new kind of sun is a
// new enum value, a loader name and one function here, nothing else changes.
struct SunPath {
  const char* name;
  // Returns the bearing (rad) after `dt` seconds of sim time.
  float (*advance)(const SunDef& def, float bearing, float dt);
};
const SunPath& sunPathFor(SunPathType type);

// BRIEF §6 beam: a surface point at world angle `phi` is in the beam iff
// cos(phi - sigma) > 0 and |R sin(phi - sigma)| <= W / 2.
bool inBeam(float planetRadius, float beamWidth, float sunBearing, float worldAngle);

// Lit arc on the planet: 2 asin(W / 2R), the whole day side when W >= 2R.
float litArc(float planetRadius, float beamWidth);

}  // namespace orbitfall
