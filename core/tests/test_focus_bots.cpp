// M2.11 acceptance (bots): on the swarm waves (content/waves/m2_swarm.json, a
// ramp of drifter swarms on both lanes that every build falls to eventually)
// a focus-builder bot beats a no-focus bot by 10 to 40%, not more (DESIGN Q34).
//
// Score: enemy hit points destroyed before the planet falls (popped or killed
// layers at full layer HP). On a ramp this tracks how much stronger a defence
// is without the all-or-nothing of "cleared / not cleared".
//
// The no-focus bot is the same player with focus fire switched off in the
// rules (no bonus, no zone preference, no Lens merge): it builds the same bent
// layout, so the gap is focus fire alone. The bent mounts themselves are
// Q33's question; for reference the test also reports never-rotate (same
// plan, straight mounts).
#include <cstdio>

#include "bots/bots.h"
#include "content/content_loader.h"
#include "doctest/doctest.h"

using namespace orbitfall;
using namespace orbitfall_bots;

namespace {

const ContentDB& shippedContent() {
  static ContentDB db;
  static bool loaded = false;
  if (!loaded) {
    std::vector<std::string> errors;
    REQUIRE(load_content(ORBITFALL_CONTENT_DIR, db, errors));
    loaded = true;
  }
  return db;
}

constexpr uint32_t kSeeds = 6;

struct Score {
  float destroyed = 0.0f;
  float waves = 0.0f;
  uint32_t focusPoints = 0;
};

Score score(const ContentDB& db, Strategy s) {
  Score out;
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    RunOptions o;
    o.seed = seed;
    o.waveSet = db.wave_set_index("m2_swarm");
    const RunResult r = play(db, s, o);
    out.destroyed += r.hpDestroyed / static_cast<float>(kSeeds);
    out.waves += static_cast<float>(r.wavesCleared) / static_cast<float>(kSeeds);
    if (r.focusPoints > out.focusPoints) out.focusPoints = r.focusPoints;
  }
  return out;
}

}  // namespace

TEST_CASE("M2.11 focus-builder beats a no-focus bot on swarm waves by 10 to 40%") {
  const ContentDB& db = shippedContent();
  REQUIRE(db.wave_set_index("m2_swarm") >= 0);
  ContentDB off = db;
  off.rules.focus.bonusPerExtra = 0.0f;
  off.rules.focus.preferZone = false;
  off.rules.focus.lensMerge = false;

  const Score focus = score(db, Strategy::FocusBuilder);
  const Score noFocus = score(off, Strategy::FocusBuilder);
  const Score straight = score(db, Strategy::NeverRotate);
  const float gain = focus.destroyed / noFocus.destroyed - 1.0f;
  std::printf("M2.11 swarm ramp (%u seeds): focus-builder %.0f HP destroyed (%.1f waves, %u focus points), "
              "no-focus %.0f (%.1f waves): %+.1f%%; never-rotate %.0f (%.1f waves): %+.1f%%\n",
              kSeeds, focus.destroyed, focus.waves, focus.focusPoints, noFocus.destroyed, noFocus.waves,
              100.0f * gain, straight.destroyed, straight.waves,
              100.0f * (focus.destroyed / straight.destroyed - 1.0f));
  CHECK(focus.focusPoints >= 2);  // it really builds focus zones (one per lane at least)
  CHECK(straight.focusPoints == 0);
  CHECK(gain >= 0.10f);
  CHECK(gain <= 0.40f);
}
