#pragma once
// Render snapshot: fills caller-provided float buffers in the BatchRenderer
// layout so the binding only has to copy them into a PackedFloat32Array.
//
// 16 floats per instance (Godot's 2D multimesh layout, see
// game/render/batch_renderer.gd):
//   0..7   Transform2D: x.x, y.x, 0, origin.x, x.y, y.y, 0, origin.y
//   8..11  colour RGBA
//   12..15 custom: shape id, colour tier (< 0 = instance colour only),
//          trait bits, extra
//
// The basis is a rotation along the velocity scaled by the entity's radius in
// world units, i.e. the batch's `shape_size` should be 2 (a shape of radius 1).
// Shape id, colour tier and trait bits come from the content's visual data.
#include <cstdint>

namespace orbitfall {

struct World;

inline constexpr uint32_t kRenderFloatsPerInstance = 16;

struct RenderParams {
  // Interpolation factor in [0, 1] (Sim::alpha()): 1 draws the latest tick,
  // lower values step back along the velocity toward the previous tick.
  float alpha = 1.0f;
  // Optional RGBA per enemy definition (4 floats each, content order), e.g.
  // resolved from the palette keys by the client. nullptr = white; enemies
  // with a colour tier are tinted by the palette in the shader anyway.
  const float* enemyColors = nullptr;
  // Placeholder projectile look, indexed by ProjectileKind (dot, shell).
  float projectileRadius[2] = {2.0f, 4.0f};
  float projectileColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// Writes up to maxInstances enemies into `out` (16 floats each); returns the
// number written. custom = shape id, colour tier, trait bits, hp fraction of
// the current layer.
uint32_t fillEnemyRenderBuffer(const World& world, float* out, uint32_t maxInstances,
                               const RenderParams& params = RenderParams());

// Same for projectiles. custom = circle shape, tier -1 (instance colour),
// 0, projectile kind.
uint32_t fillProjectileRenderBuffer(const World& world, float* out, uint32_t maxInstances,
                                    const RenderParams& params = RenderParams());

}  // namespace orbitfall
