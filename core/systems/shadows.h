#pragma once
// M1.8 shadows (BRIEF §6). Everything is done in "beam space": y is the
// distance of a point from the sun line through the planet centre, measured
// across the beam, so a surface point at world angle phi has
// y = R sin(phi - sigma) and a caster disc shades one y interval.
#include <cstdint>

namespace orbitfall {

struct World;

struct ShadeInterval {
  float lo = 0.0f, hi = 0.0f;
};

// Interval shaded by a disc at (px, py) with radius r, for sun direction
// (sx, sy) and planet radius R. A caster casts iff dot(p, s) > R; its interval
// is [o - r, o + r] with o = s.x p.y - s.y p.x. Returns false when it casts
// nothing on the planet.
bool casterInterval(float px, float py, float r, float sx, float sy, float planetRadius,
                    ShadeInterval& out);

// Sorts and merges overlapping intervals in place; returns the new count.
uint32_t mergeIntervals(ShadeInterval* intervals, uint32_t count);

// True when y lies in one of the merged (sorted) intervals.
bool isShaded(const ShadeInterval* intervals, uint32_t count, float y);

// The world's merged shadows as world-angle pairs [a0, a1] on the lit side,
// clipped to the beam (phi = sigma + asin(y / R)). Writes up to maxPairs pairs
// (2 floats each) and returns the number written.
uint32_t shadeIntervalAngles(const World& world, float* out, uint32_t maxPairs);

// Where an Eclipser of this radius wants to sit: the offset from the sun line
// (in y) whose shadow covers the most lit panels, centred on the panels it
// covers; 0 when no panel is lit. Used by the Eclipser's movement and by the
// forecast's ghost arc, so the two cannot disagree.
float eclipserOffset(const World& world, float casterRadius);

}  // namespace orbitfall
