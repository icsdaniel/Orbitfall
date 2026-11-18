// M2.8 content tests: the three home planets in content/planets/*.json load,
// medium is exactly the M1 planet, and size drives the four stats the way
// PLAN §5.4 says (small: fast, fragile, few slots, wide lit arc; large: the
// opposite). Lit arc follows from the beam: 2 asin(W / 2R) (BRIEF §6).
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/mathx.h"
#include "systems/sun.h"

#ifndef ORBITFALL_CONTENT_DIR
#define ORBITFALL_CONTENT_DIR "../content"
#endif

using namespace orbitfall;

namespace {

constexpr float kDegP = kPi / 180.0f;

const ContentDB& db() {
  static ContentDB content;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, content, errors));
    loaded = true;
  }
  return content;
}

const PlanetDef& planet(const char* id) {
  const int i = db().planet_index(id);
  REQUIRE_MESSAGE(i != kNoIndex, id);
  return db().planets[static_cast<size_t>(i)];
}

}  // namespace

TEST_SUITE("content") {

TEST_CASE("home planets: small, medium and large ship with display names") {
  CHECK(db().planets.size() == 3);
  CHECK(planet("small").name == "Small");
  CHECK(planet("medium").name == "Medium");
  CHECK(planet("large").name == "Large");
}

TEST_CASE("home planets: medium is the M1 planet, unchanged") {
  const PlanetDef& m = planet("medium");
  CHECK(m.radius == doctest::Approx(100));
  CHECK(m.slots == 36);
  CHECK(m.hp == doctest::Approx(100));
  CHECK(m.rotMaxSpeed == doctest::Approx(90 * kDegP));
  CHECK(m.rotAccel == doctest::Approx(360 * kDegP));
  CHECK(db().waveSets[0].planet == db().planet_index("medium"));
}

TEST_CASE("home planets: size drives rotation, HP, slots and lit fraction") {
  const PlanetDef& s = planet("small");
  const PlanetDef& m = planet("medium");
  const PlanetDef& l = planet("large");
  const float beam = db().suns[static_cast<size_t>(db().waveSets[0].sun)].beamWidth;
  CHECK(s.radius < m.radius);
  CHECK(m.radius < l.radius);
  // Small is fast, large is slow (top speed and how quickly it gets there).
  CHECK(s.rotMaxSpeed > m.rotMaxSpeed);
  CHECK(m.rotMaxSpeed > l.rotMaxSpeed);
  CHECK(s.rotAccel > m.rotAccel);
  CHECK(m.rotAccel > l.rotAccel);
  // Bigger planets are tougher and have more room.
  CHECK(s.hp < m.hp);
  CHECK(m.hp < l.hp);
  CHECK(s.slots < m.slots);
  CHECK(m.slots < l.slots);
  // The beam is the same width, so a bigger planet has a smaller lit fraction.
  // Every planet must be wider than the beam, or the whole day side is lit.
  for (const PlanetDef* p : {&s, &m, &l}) CHECK(2.0f * p->radius > beam);
  const float fs = litArc(s.radius, beam) / kTau, fm = litArc(m.radius, beam) / kTau,
              fl = litArc(l.radius, beam) / kTau;
  CHECK(fs > fm);
  CHECK(fm > fl);
  CHECK(fm == doctest::Approx(0.25).epsilon(0.01));
}

TEST_CASE("home planets: every planet can host every wave set (same lanes)") {
  for (size_t w = 0; w < db().waveSets.size(); ++w) {
    for (size_t p = 0; p < db().planets.size(); ++p) {
      CAPTURE(db().planets[p].id);
      CHECK(db().planet_fits_wave_set(static_cast<int>(w), static_cast<int>(p)));
    }
  }
}

TEST_CASE("home planets: a planet without the wave set's lanes does not fit") {
  ContentDB copy = db();
  PlanetDef& s = copy.planets[static_cast<size_t>(copy.planet_index("small"))];
  s.lanes[1].id = "C";
  CHECK_FALSE(copy.planet_fits_wave_set(0, copy.planet_index("small")));
  CHECK(copy.planet_fits_wave_set(0, copy.planet_index("medium")));
  CHECK_FALSE(copy.planet_fits_wave_set(0, 99));
  CHECK_FALSE(copy.planet_fits_wave_set(7, 0));
}

}  // TEST_SUITE
