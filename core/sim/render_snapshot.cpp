#include "sim/render_snapshot.h"

#include <cmath>

#include "sim/world.h"
#include "systems/enemies.h"

namespace orbitfall {
namespace {

// Seconds to step back along the velocity for interpolation; 0 while frozen.
float backstep(const World& world, float alpha) {
  if (!world.running()) return 0.0f;
  const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
  return (1.0f - a) * world.dt;
}

// Transform2D rows for a shape of radius `scale` at (x, y) whose "up" (the
// shader's -y) points along (vx, vy); unrotated when it is not moving.
void writeTransform(float* o, float x, float y, float vx, float vy, float scale) {
  float dx = 0.0f, dy = -1.0f;
  const float len = std::sqrt(vx * vx + vy * vy);
  if (len > 1e-6f) {
    dx = vx / len;
    dy = vy / len;
  }
  // Rotation taking (0, -1) to (dx, dy): x axis = (-dy, dx), y axis = (-dx, -dy).
  o[0] = -dy * scale;  // x.x
  o[1] = -dx * scale;  // y.x
  o[2] = 0.0f;
  o[3] = x;
  o[4] = dx * scale;   // x.y
  o[5] = -dy * scale;  // y.y
  o[6] = 0.0f;
  o[7] = y;
}

}  // namespace

uint32_t fillEnemyRenderBuffer(const World& world, float* out, uint32_t maxInstances,
                               const RenderParams& params) {
  if (world.content == nullptr || out == nullptr) return 0;
  const ContentDB& db = *world.content;
  const EnemyPool& e = world.enemies;
  const float back = backstep(world, params.alpha);
  const uint32_t n = e.count < maxInstances ? e.count : maxInstances;
  for (uint32_t i = 0; i < n; ++i) {
    const EnemyDef& def = db.enemies[e.type[i]];
    float* o = out + static_cast<size_t>(i) * kRenderFloatsPerInstance;
    writeTransform(o, e.posX[i] - e.velX[i] * back, e.posY[i] - e.velY[i] * back, e.velX[i],
                   e.velY[i], def.size);
    const float* color = params.enemyColors ? params.enemyColors + 4u * e.type[i] : nullptr;
    for (int c = 0; c < 4; ++c) o[8 + c] = color ? color[c] : 1.0f;
    o[12] = static_cast<float>(static_cast<int>(def.visual.shape));
    o[13] = static_cast<float>(def.visual.colorTier);
    o[14] = static_cast<float>(e.traits[i]);
    const float full = layerHp(def);
    o[15] = full > 0.0f ? e.hp[i] / full : 0.0f;
  }
  return n;
}

uint32_t fillProjectileRenderBuffer(const World& world, float* out, uint32_t maxInstances,
                                    const RenderParams& params) {
  if (world.content == nullptr || out == nullptr) return 0;
  const ContentDB& db = *world.content;
  const ProjectilePool& p = world.projectiles;
  const float back = backstep(world, params.alpha);
  const uint32_t n = p.count < maxInstances ? p.count : maxInstances;
  for (uint32_t i = 0; i < n; ++i) {
    const int kind = static_cast<int>(db.buildings[p.type[i]].projectile.kind);
    float* o = out + static_cast<size_t>(i) * kRenderFloatsPerInstance;
    writeTransform(o, p.posX[i] - p.velX[i] * back, p.posY[i] - p.velY[i] * back, p.velX[i],
                   p.velY[i], params.projectileRadius[kind < 2 ? kind : 0]);
    for (int c = 0; c < 4; ++c) o[8 + c] = params.projectileColor[c];
    o[12] = static_cast<float>(static_cast<int>(Shape::Circle));
    o[13] = -1.0f;
    o[14] = 0.0f;
    o[15] = static_cast<float>(kind);
  }
  return n;
}

}  // namespace orbitfall
