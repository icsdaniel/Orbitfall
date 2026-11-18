// M2.8 acceptance: each home planet changes all four stats IN PLAY (a real
// run on the shipped wave set): rotation speed reached under full input, HP,
// slot count and the fraction of slots the beam lights.
#include <string>
#include <vector>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/sun.h"
#include "systems/waves.h"

using namespace orbitfall;

namespace {

const ContentDB& shipped() {
  static ContentDB content;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, content, errors));
    loaded = true;
  }
  return content;
}

struct InPlay {
  float topSpeed = 0.0f;   // rad/s after a second of full rotate input
  float hpMax = 0.0f;
  uint32_t slots = 0;
  float litFraction = 0.0f;  // lit slots / slots, measured from the slot flags
  float radius = 0.0f;
};

InPlay measure(int planet) {
  SimConfig cfg;
  cfg.seed = 3;
  cfg.content = &shipped();
  cfg.waveSet = 0;
  cfg.planetOverride = planet;
  Sim sim(cfg);
  World& w = sim.world();
  InPlay out;
  out.hpMax = w.planetHpMax;
  out.slots = w.slotCount();
  out.radius = w.planet->radius;
  sim.step();  // flags are refreshed on the first tick
  uint32_t lit = 0;
  for (const BuildingSlot& b : w.slots) lit += (b.flags & kSlotLit) != 0 ? 1u : 0u;
  out.litFraction = static_cast<float>(lit) / static_cast<float>(out.slots);
  sim.push(cmd::rotateInput(sim.tick(), 1.0f));  // rotation is live between waves
  for (int i = 0; i < 30; ++i) sim.step();
  out.topSpeed = w.planetAngVel;
  return out;
}

}  // namespace

TEST_CASE("home planets: no override plays the wave set's own planet (medium)") {
  const ContentDB& db = shipped();
  SimConfig cfg;
  cfg.content = &db;
  cfg.waveSet = 0;
  Sim sim(cfg);
  CHECK(sim.world().planet == &db.planets[static_cast<size_t>(db.planet_index("medium"))]);
  World other;
  initWorld(other, db, 0, 1, db.planet_index("large"));
  CHECK(other.planet->id == "large");
  initWorld(other, db, 0, 1);
  CHECK(other.planet->id == "medium");
}

TEST_CASE("home planets: each planet changes all four stats in play") {
  const ContentDB& db = shipped();
  const InPlay s = measure(db.planet_index("small"));
  const InPlay m = measure(db.planet_index("medium"));
  const InPlay l = measure(db.planet_index("large"));
  // Rotation speed: small fast, large slow.
  CHECK(s.topSpeed > m.topSpeed);
  CHECK(m.topSpeed > l.topSpeed);
  CHECK(s.topSpeed == doctest::Approx(db.planets[static_cast<size_t>(db.planet_index("small"))].rotMaxSpeed));
  // HP and slots grow with size.
  CHECK(s.hpMax < m.hpMax);
  CHECK(m.hpMax < l.hpMax);
  CHECK(s.slots < m.slots);
  CHECK(m.slots < l.slots);
  // Lit fraction shrinks with size, and matches the BRIEF §6 lit arc to a slot.
  CHECK(s.litFraction > m.litFraction);
  CHECK(m.litFraction > l.litFraction);
  const float beam = db.suns[0].beamWidth;
  for (const InPlay* p : {&s, &m, &l}) {
    CAPTURE(p->radius);
    CHECK(p->litFraction == doctest::Approx(litArc(p->radius, beam) / kTau).epsilon(1.5f / static_cast<float>(p->slots) /
                                                                                       (litArc(p->radius, beam) / kTau)));
  }
}

TEST_CASE("home planets: a planet that does not fit the wave set falls back to its own") {
  ContentDB db = shipped();
  const int small = db.planet_index("small");
  db.planets[static_cast<size_t>(small)].lanes[0].id = "Z";
  World w;
  initWorld(w, db, 0, 1, small);
  CHECK(w.planet->id == "medium");
}

TEST_CASE("home planets: a run on another planet replays to the same hash") {
  const ContentDB& db = shipped();
  SimConfig cfg;
  cfg.seed = 11;
  cfg.content = &db;
  cfg.waveSet = 0;
  cfg.planetOverride = db.planet_index("small");
  Sim sim(cfg);
  sim.push(cmd::placeBuilding(0, 2, db.building_index("gatling"), 0.0f));
  sim.push(cmd::startWave(0));
  for (int i = 0; i < 300; ++i) sim.step();
  const uint64_t h = sim.hash();
  CHECK(replayHash(cfg, sim.world().commandLog, sim.tick()) == h);
  SimConfig medium = cfg;
  medium.planetOverride = kNoIndex;
  CHECK(replayHash(medium, sim.world().commandLog, sim.tick()) != h);
}
