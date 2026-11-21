// M2.6 towers batch 1 on tiny worlds: each tower beats its trait at least 2x
// over a tower that is not listed against it, Flak falloff, Railgun pierce,
// Cryo slow, Radar camo reveal, Amplifier shield damage, and the Sun Lens
// (ramp, channel 20% -> 100%, Eclipser shade, Overcharge with a
// light-charged cooldown).
//
// The worlds are sandbox runs (no waves) on the shipped tower definitions, so
// these tests follow the shipped numbers; the targets are made-up enemies
// that never move and never die (huge hit points), so damage dealt is simply
// the hit points and shield points they lost.
#include <cmath>
#include <string>

#include "content/content_loader.h"
#include "doctest/doctest.h"
#include "sim/mathx.h"
#include "sim/sim.h"
#include "systems/enemies.h"
#include "systems/towers.h"

using namespace orbitfall;

namespace {

// Big enough never to die in a test, small enough that float hit points
// still resolve a dim beam tick (about 0.01).
constexpr float kHuge = 1000.0f;
constexpr uint32_t kSlots = 36;  // the medium planet
constexpr int kTowerSlot = 18;   // world angle 180 deg: the dark side at the start (sun at 0 deg)

ContentDB shipped() {
  ContentDB db;
  std::vector<std::string> errors;
  REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
  return db;
}

// A target that stands still and never dies; `traits` / `shield` as asked.
int addTarget(ContentDB& db, uint32_t traits, float shield = 0.0f, bool shadow = false,
              float size = 8.0f) {
  EnemyDef e = db.enemies[static_cast<size_t>(db.enemy_index("drifter_red"))];
  e.id = "target" + std::to_string(db.enemies.size());
  e.behavior = EnemyBehavior::Lane;
  e.hasHover = false;
  e.layers = 1;
  e.hp = e.hpPerLayer = kHuge;
  e.speed = 0.0f;
  e.size = size;
  e.bounty = 0.0f;
  e.traits = traits;
  e.shield = shield;
  if (shield > 0.0f) e.traits |= kTraitShielded;
  e.shadowCaster = shadow;
  e.popsInto = kNoIndex;
  db.enemies.push_back(e);
  db.enemyIds[e.id] = static_cast<int>(db.enemies.size() - 1);
  return static_cast<int>(db.enemies.size() - 1);
}

SimConfig sandbox(const ContentDB& db) {
  SimConfig cfg;
  cfg.seed = 7;
  cfg.content = &db;
  cfg.waveSet = kNoIndex;
  cfg.planet = db.planet_index("medium");
  cfg.sun = db.sun_index("default");
  cfg.enemyCapacity = 256;
  cfg.projectileCapacity = 1024;
  return cfg;
}

void place(Sim& sim, int site, const ContentDB& db, const char* id) {
  sim.push(cmd::placeBuilding(sim.tick(), site, db.building_index(id), 0.0f));
  sim.step();
  REQUIRE(sim.world().site(static_cast<uint32_t>(site)).type == db.building_index(id));
}

// Puts enemy `type` at world angle `angle`, `dist` above the surface.
uint32_t putAt(World& w, int type, float angle, float dist) {
  const uint32_t i = spawnEnemy(w, type, 0);
  REQUIRE(i != kInvalidIndex);
  const float r = w.planet->radius + dist;
  w.enemies.posX[i] = r * std::cos(angle);
  w.enemies.posY[i] = r * std::sin(angle);
  w.enemies.velX[i] = 0.0f;
  w.enemies.velY[i] = 0.0f;
  return i;
}

float lost(const World& w, uint32_t i, float shield0) {
  return (kHuge - w.enemies.hp[i]) + (shield0 - w.enemies.shield[i]);
}

float slotAngle(int slot) { return 2.0f * kPi * static_cast<float>(slot) / static_cast<float>(kSlots); }

struct Layout {
  const char* tower = nullptr;
  const char* left = nullptr;   // building on the slot before the tower
  const char* right = nullptr;  // building on the slot after it
  std::vector<int> panels;      // solar panel slots
};

// Damage `layout`'s tower deals in `seconds` to one target of type `enemy`
// standing `dist` in front of it.
float dealt(const ContentDB& db, const Layout& layout, int enemy, float seconds, float dist = 120.0f) {
  Sim sim(sandbox(db));
  sim.world().stardust = 1.0e6f;
  for (int s : layout.panels) place(sim, s, db, "solar_panel");
  if (layout.left) place(sim, kTowerSlot - 1, db, layout.left);
  if (layout.right) place(sim, kTowerSlot + 1, db, layout.right);
  place(sim, kTowerSlot, db, layout.tower);
  const uint32_t i = putAt(sim.world(), enemy, slotAngle(kTowerSlot), dist);
  const float shield0 = sim.world().enemies.shield[i];
  const int ticks = static_cast<int>(seconds * 30.0f);
  for (int t = 0; t < ticks; ++t) sim.step();
  return lost(sim.world(), i, shield0);
}

// How much better `layout` does against `trait` than against a plain target.
float traitGain(const ContentDB& db, const Layout& layout, int plain, int trait, float seconds = 12.0f) {
  const float a = dealt(db, layout, plain, seconds);
  const float b = dealt(db, layout, trait, seconds);
  REQUIRE(a > 0.0f);
  return b / a;
}

}  // namespace

TEST_CASE("M2.6 Flak beats Armored at least 2x over the Gatling, and hits hardest close") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), armored = addTarget(db, kTraitArmored);
  const float flak = traitGain(db, {"flak"}, plain, armored);
  const float gatling = traitGain(db, {"gatling"}, plain, armored);
  CAPTURE(flak);
  CAPTURE(gatling);
  CHECK(flak / gatling >= 1.999f);
  // Falloff: strong close, weak far (data: 1.6 at 0, 0.5 at full range).
  const float close = dealt(db, {"flak"}, plain, 6.0f, 20.0f);
  const float far = dealt(db, {"flak"}, plain, 6.0f, 215.0f);
  CHECK(close > 2.0f * far);
}

TEST_CASE("M2.6 Railgun beats Armored at least 2x over the Gatling; its shots pierce") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), armored = addTarget(db, kTraitArmored);
  const float rail = traitGain(db, {"railgun"}, plain, armored);
  const float gatling = traitGain(db, {"gatling"}, plain, armored);
  CHECK(rail / gatling >= 2.0f);

  // Three targets in a row: one slug (pierce 2) damages all of them; the
  // falloff is flat, so the far one takes as much as the near one.
  Sim sim(sandbox(db));
  sim.world().stardust = 1.0e6f;
  place(sim, kTowerSlot, db, "railgun");
  const uint32_t a = putAt(sim.world(), plain, slotAngle(kTowerSlot), 100.0f);
  const uint32_t b = putAt(sim.world(), plain, slotAngle(kTowerSlot), 250.0f);
  const uint32_t c = putAt(sim.world(), plain, slotAngle(kTowerSlot), 500.0f);
  for (int t = 0; t < 20; ++t) sim.step();  // one shot (0.45/s) lands
  CHECK(lost(sim.world(), a, 0.0f) > 0.0f);
  CHECK(lost(sim.world(), b, 0.0f) == doctest::Approx(lost(sim.world(), a, 0.0f)));
  CHECK(lost(sim.world(), c, 0.0f) == doctest::Approx(lost(sim.world(), a, 0.0f)));
}

TEST_CASE("M2.6 Cryo beats Heat-resistant at least 2x over the Sun Lens and slows what it hits") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), heat = addTarget(db, kTraitHeatResistant);
  const float cryo = traitGain(db, {"cryo"}, plain, heat);
  const float lens = traitGain(db, {"sun_lens"}, plain, heat);
  CHECK(cryo / lens >= 2.0f);

  // Slow: a Fast dart walking in under a Cryo covers about 40% less ground.
  const int dart = db.enemy_index("dart");
  const auto walked = [&](bool withCryo) {
    Sim sim(sandbox(db));
    sim.world().stardust = 1.0e6f;
    if (withCryo) place(sim, kTowerSlot, db, "cryo");
    World& w = sim.world();
    const uint32_t i = putAt(w, dart, slotAngle(kTowerSlot), 270.0f);
    const float speed = db.enemies[static_cast<size_t>(dart)].speed;
    w.enemies.velX[i] = speed;  // inward: slot 18 is at world angle 180 deg
    w.enemies.velY[i] = 0.0f;
    w.enemies.hp[i] = kHuge;
    const float x0 = w.enemies.posX[i];
    for (int t = 0; t < 45; ++t) sim.step();
    return w.enemies.posX[i] - x0;
  };
  const float free = walked(false), slowed = walked(true);
  CHECK(slowed < 0.8f * free);
  CHECK(slowed > 0.4f * free);
}

TEST_CASE("M2.6 Radar reveals camo: a Gatling beside it beats Camo 2x over one without") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), camo = addTarget(db, kTraitCamo);
  REQUIRE(db.damage.traits[0].missChance == doctest::Approx(0.5f));
  const float with = traitGain(db, {"gatling", nullptr, "radar"}, plain, camo);
  const float without = traitGain(db, {"gatling"}, plain, camo);
  CAPTURE(with);
  CAPTURE(without);
  CHECK(with == doctest::Approx(1.0f));      // nothing misses
  CHECK(without <= 0.5f + 1e-4f);            // every other shot misses
  CHECK(with / without >= 2.0f);
  // Radar is an aura: two slots away still counts, the radius is data.
  Sim sim(sandbox(db));
  sim.world().stardust = 1.0e6f;
  place(sim, kTowerSlot, db, "gatling");
  place(sim, kTowerSlot + 2, db, "radar");
  CHECK(sim.world().site(kTowerSlot).detect == doctest::Approx(1.0f));
  place(sim, kTowerSlot + 6, db, "gatling");  // 4 slots from the Radar
  CHECK(sim.world().site(kTowerSlot + 6).detect == 0.0f);
  // Wide Sweep: tier 1 and tier 3 widen the reach by one slot each.
  sim.push(cmd::upgradeBuilding(sim.tick(), kTowerSlot + 2, 0));
  sim.step();
  CHECK(sim.world().site(kTowerSlot + 6).detect == 0.0f);  // reach 3: still out
  sim.push(cmd::upgradeBuilding(sim.tick(), kTowerSlot + 2, 0));
  sim.push(cmd::upgradeBuilding(sim.tick(), kTowerSlot + 2, 0));
  sim.step();
  CHECK(sim.world().site(kTowerSlot + 2).tiers[0] == 3);
  CHECK(sim.world().site(kTowerSlot + 6).detect > 0.0f);   // Phased Array: reach 4
}

TEST_CASE("M2.6 Amplifier beats Shielded 2x over a Radar (non-listed support) and adds damage") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), shielded = addTarget(db, 0, kHuge);
  const float amp = traitGain(db, {"gatling", "amplifier"}, plain, shielded);
  const float radar = traitGain(db, {"gatling", "radar"}, plain, shielded);
  CHECK(amp / radar >= 1.999f);
  // +15% damage on plain targets (data).
  const float boosted = dealt(db, {"gatling", "amplifier"}, plain, 6.0f);
  const float bare = dealt(db, {"gatling"}, plain, 6.0f);
  CHECK(boosted / bare == doctest::Approx(1.15f).epsilon(0.01));
}

TEST_CASE("M2.6 Sun Lens beats Armored at least 2x over the Gatling") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0), armored = addTarget(db, kTraitArmored);
  const float lens = traitGain(db, {"sun_lens"}, plain, armored);
  const float gatling = traitGain(db, {"gatling"}, plain, armored);
  CHECK(lens / gatling >= 1.999f);
}

TEST_CASE("M2.6 Sun Lens: channel factor and ramp follow the data") {
  const ContentDB db = shipped();
  const BuildingDef& lens = db.buildings[static_cast<size_t>(db.building_index("sun_lens"))];
  CHECK(channelFactor(lens, 0.0f) == doctest::Approx(0.2f));
  CHECK(channelFactor(lens, 0.5f) == doctest::Approx(0.6f));
  CHECK(channelFactor(lens, 1.0f) == doctest::Approx(1.0f));
  CHECK(rampFactor(lens, 0.0f) == doctest::Approx(0.3f));
  CHECK(rampFactor(lens, 1.5f) == doctest::Approx(1.0f));
  CHECK(rampFactor(lens, 9.0f) == doctest::Approx(1.0f));
}

TEST_CASE("M2.6 Sun Lens damage is ~20% with no panel lit and 100% with all panels lit") {
  ContentDB db = shipped();
  db.suns[static_cast<size_t>(db.sun_index("default"))].speed = 0.0f;  // the beam stays put
  const int plain = addTarget(db, 0);
  // The sun starts at 0 deg: slots 35, 0, 1 are in the beam; 9 and 27 are not.
  const Layout lit{"sun_lens", nullptr, nullptr, {35, 0, 1}};
  const Layout dark{"sun_lens", nullptr, nullptr, {9, 27}};
  const Layout none{"sun_lens"};
  // Measure after the ramp: damage from second 2 to second 6.
  const auto steady = [&](const Layout& l) {
    return dealt(db, l, plain, 6.0f) - dealt(db, l, plain, 2.0f);
  };
  const float full = steady(lit), unlit = steady(dark), nopanels = steady(none);
  CAPTURE(full);
  CAPTURE(unlit);
  CHECK(unlit / full == doctest::Approx(0.2f).epsilon(0.02));
  CHECK(nopanels / full == doctest::Approx(0.2f).epsilon(0.02));

  // The channel value itself.
  Sim sim(sandbox(db));
  sim.world().stardust = 1.0e6f;
  for (int s : lit.panels) place(sim, s, db, "solar_panel");
  CHECK(sim.world().channel == doctest::Approx(1.0f));
  place(sim, 9, db, "solar_panel");
  CHECK(sim.world().channel == doctest::Approx(0.75f));
}

TEST_CASE("M2.6 Sun Lens ramps up on one target") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0);
  const Layout lit{"sun_lens", nullptr, nullptr, {35, 0, 1}};
  const float first = dealt(db, lit, plain, 0.5f);
  const float later = dealt(db, lit, plain, 4.5f) - dealt(db, lit, plain, 4.0f);
  CHECK(later > 2.0f * first);
}

TEST_CASE("M2.6 an Eclipser shading the panels weakens the Sun Lens beam") {
  ContentDB db = shipped();
  db.suns[static_cast<size_t>(db.sun_index("default"))].speed = 0.0f;  // the shadow stays put
  const int plain = addTarget(db, 0);
  const int shade = addTarget(db, 0, 0.0f, true, 28.0f);  // a parked shadow caster
  const auto run = [&](bool eclipse, float& channel, float& power) {
    Sim sim(sandbox(db));
    World& w = sim.world();
    w.stardust = 1.0e6f;
    for (int s : {35, 0, 1}) place(sim, s, db, "solar_panel");
    place(sim, kTowerSlot, db, "sun_lens");
    const uint32_t t = putAt(w, plain, slotAngle(kTowerSlot), 120.0f);
    if (eclipse) putAt(w, shade, 0.0f, 150.0f);  // between the sun and the panels
    for (int k = 0; k < 90; ++k) sim.step();
    const float before = lost(w, t, 0.0f);
    for (int k = 0; k < 90; ++k) sim.step();
    channel = w.channel;
    power = w.beams.empty() ? 0.0f : w.beams[0].power;
    return lost(w, t, 0.0f) - before;
  };
  float chLit = 0, chShade = 0, pLit = 0, pShade = 0;
  const float bright = run(false, chLit, pLit);
  const float dim = run(true, chShade, pShade);
  CHECK(chLit == doctest::Approx(1.0f));
  CHECK(chShade == doctest::Approx(0.0f));
  CHECK(pShade < 0.5f * pLit);  // the drawn beam is visibly weaker too
  CHECK(dim / bright == doctest::Approx(0.2f).epsilon(0.02));
}

TEST_CASE("M2.6 Overcharge: activate_building boosts the beam; light charges the cooldown 2x") {
  ContentDB db = shipped();
  const int plain = addTarget(db, 0);
  const BuildingDef& lensDef = db.buildings[static_cast<size_t>(db.building_index("sun_lens"))];
  REQUIRE(lensDef.hasActive);

  Sim sim(sandbox(db));
  World& w = sim.world();
  w.stardust = 1.0e6f;
  for (int s : {35, 0, 1}) place(sim, s, db, "solar_panel");
  place(sim, kTowerSlot, db, "sun_lens");  // dark side
  place(sim, 2, db, "sun_lens");           // in the beam
  const uint32_t t = putAt(w, plain, slotAngle(kTowerSlot), 120.0f);
  for (int k = 0; k < 60; ++k) sim.step();  // past the ramp
  float before = lost(w, t, 0.0f);
  for (int k = 0; k < 30; ++k) sim.step();
  const float normal = lost(w, t, 0.0f) - before;

  w.events.clear();
  sim.push(cmd::activateBuilding(sim.tick(), kTowerSlot));
  sim.push(cmd::activateBuilding(sim.tick(), 2));
  sim.step();
  bool activated = false;
  for (const Event& e : w.events.items)
    activated = activated || (e.type == EventType::BuildingActivated && e.a == kTowerSlot);
  CHECK(activated);
  before = lost(w, t, 0.0f);
  for (int k = 0; k < 30; ++k) sim.step();
  const float boosted = lost(w, t, 0.0f) - before;
  CHECK(boosted / normal == doctest::Approx(lensDef.active.damageMult).epsilon(0.02));

  // Not ready again: rejected with ActiveNotReady.
  w.events.clear();
  sim.push(cmd::activateBuilding(sim.tick(), kTowerSlot));
  sim.step();
  bool notReady = false;
  for (const Event& e : w.events.items)
    notReady = notReady || (e.type == EventType::CommandRejected &&
                            e.b == static_cast<int32_t>(RejectReason::ActiveNotReady));
  CHECK(notReady);

  // The lit Lens comes back about twice as fast as the dark one.
  int litReady = -1, darkReady = -1;
  for (int k = 0; k < 30 * 40 && darkReady < 0; ++k) {
    sim.step();
    if (litReady < 0 && w.site(2).activeCooldown <= 0.0f) litReady = k;
    if (darkReady < 0 && w.site(kTowerSlot).activeCooldown <= 0.0f) darkReady = k;
  }
  REQUIRE(litReady > 0);
  REQUIRE(darkReady > 0);
  CHECK(static_cast<float>(darkReady) / static_cast<float>(litReady) ==
        doctest::Approx(db.rules.lightChargeMult).epsilon(0.1));

  // A building without an active refuses.
  w.events.clear();
  sim.push(cmd::activateBuilding(sim.tick(), 0));
  sim.step();
  bool refused = false;
  for (const Event& e : w.events.items)
    refused = refused || (e.type == EventType::CommandRejected &&
                          e.b == static_cast<int32_t>(RejectReason::NotAllowed));
  CHECK(refused);
}

TEST_CASE("M2.6 towers keep runs deterministic") {
  ContentDB db = shipped();
  const int plain = addTarget(db, kTraitCamo, 5.0f);
  const auto run = [&]() {
    Sim sim(sandbox(db));
    sim.world().stardust = 1.0e6f;
    const char* ids[] = {"flak", "railgun", "cryo", "radar", "amplifier", "sun_lens"};
    int s = 14;
    for (const char* id : ids) place(sim, s++, db, id);
    for (int k = 0; k < 6; ++k) putAt(sim.world(), plain, slotAngle(14 + k), 60.0f + 30.0f * k);
    sim.push(cmd::activateBuilding(sim.tick(), 19));
    for (int k = 0; k < 300; ++k) sim.step();
    return sim.hash();
  };
  CHECK(run() == run());
}
