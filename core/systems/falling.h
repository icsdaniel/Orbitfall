#pragma once
// M2.10 falling objects and shields (PLAN §5.4b).
//
// Falling objects (meteors from wave volleys, bombs dropped by Bombers) come
// straight down a fixed world bearing. They are telegraphed: each object is
// "warned" once it is rules.fallingWarnSeconds (3 s) from impact. A Shield
// building protects the sky above its slot: an object that is under a shield's
// dome (distance <= R + height) and inside its arc while the shield is online
// is absorbed and drains the shield's energy by the object's damage; at 0 the
// shield goes offline and recharges over rechargeSeconds, lightChargeMult
// times faster while its slot is lit and unshaded. Anything that reaches the
// surface subtracts planet HP only: buildings are never damaged (Q8, Q22).
#include <cstdint>

#include "content/content.h"

namespace orbitfall {

struct World;

// FallingPool::flags bits.
enum FallingFlag : uint16_t {
  kFallingDead = 1u << 0,    // absorbed or landed this tick; removed by fallingImpact
  kFallingWarned = 1u << 1,  // FallingWarning was emitted
};

// Adds one falling object of `type` (index into ContentDB::falling) at
// distance `dist` on world bearing `bearing`. Returns its pool index, or
// kInvalidIndex for a bad type or a full pool.
uint32_t spawnFalling(World& world, int type, float bearing, float dist);

// Seconds until falling object `i` reaches the surface (0 when it has).
float fallingEta(const World& world, uint32_t i);

// Radius of a shield's dome around the planet centre.
float shieldDomeRadius(const World& world, const BuildingDef& def);

// The slot of the online shield whose arc covers world bearing `bearing`
// (the one with the most energy left; the lowest slot on a tie), or -1.
// Ignores the dome height: "would a shield catch an object coming down here".
int shieldCovering(const World& world, float bearing);

// Sim time into a wave at which object `k` (0-based) of a volley is released.
inline float volleyTime(const VolleyDef& v, int k) {
  return v.startSeconds + (v.count > 0 ? v.seconds * static_cast<float>(k) / static_cast<float>(v.count) : 0.0f);
}

// --- systems (registered in core/sim/pipeline.cpp after projectile movement) ---

// Moves falling objects down their bearing; emits FallingWarning once an
// object is within rules.fallingWarnSeconds of impact.
void fallingObjects(World& world, float dt);
// Recharges shields (offline: back online after rechargeSeconds of charge;
// online: regenPerSec), both lightChargeMult times faster while the slot is lit
// and unshaded; charges upkeepPerSec stardust while online. Then absorbs every
// falling object under an online shield's dome and arc (ShieldAbsorbed,
// ShieldOffline at 0 energy).
void shields(World& world, float dt);
// Objects that reached the surface subtract planet HP (FallingImpact, GameOver
// at 0); absorbed and landed objects are removed.
void fallingImpact(World& world, float dt);

}  // namespace orbitfall
