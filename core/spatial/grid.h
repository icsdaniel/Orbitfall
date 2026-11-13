#pragma once
// Uniform spatial grid over points, rebuilt each tick by counting sort
// (BRIEF §5.3). Storage is reserved once in configure(); rebuild() and query()
// never allocate. Points outside the covered square land in the edge cells, so
// nothing is ever lost, only less tightly culled.
#include <cstdint>
#include <vector>

namespace orbitfall {

struct SpatialGrid {
  float minX = 0.0f, minY = 0.0f;
  float cellSize = 1.0f, invCellSize = 1.0f;
  uint32_t cols = 0, rows = 0;
  uint32_t count = 0;               // points in the last rebuild
  std::vector<uint32_t> cellStart;  // cols*rows + 1 offsets into items
  std::vector<uint32_t> items;      // point indices grouped by cell
  std::vector<uint32_t> cellOf;     // scratch: cell of each point

  // Covers [-halfExtent, halfExtent]^2 with cellsPerSide^2 cells for up to
  // `capacity` points. The only place that allocates.
  void configure(float halfExtent, uint32_t cellsPerSide, uint32_t capacity);

  // Re-sorts `n` points (n <= capacity) into cells.
  void rebuild(const float* x, const float* y, uint32_t n);

  uint32_t cellX(float x) const { return clampCell((x - minX) * invCellSize, cols); }
  uint32_t cellY(float y) const { return clampCell((y - minY) * invCellSize, rows); }

  // Calls fn(pointIndex) for every point in the cells touching the box. The
  // caller does the exact distance test.
  template <typename Fn>
  void query(float x0, float y0, float x1, float y1, Fn&& fn) const {
    if (cols == 0 || count == 0) return;
    const uint32_t cx0 = cellX(x0), cx1 = cellX(x1), cy0 = cellY(y0), cy1 = cellY(y1);
    for (uint32_t cy = cy0; cy <= cy1; ++cy) {
      for (uint32_t cx = cx0; cx <= cx1; ++cx) {
        const uint32_t cell = cy * cols + cx;
        for (uint32_t k = cellStart[cell]; k < cellStart[cell + 1]; ++k) fn(items[k]);
      }
    }
  }

 private:
  static uint32_t clampCell(float c, uint32_t n) {
    if (!(c > 0.0f)) return 0;  // also NaN
    const float top = static_cast<float>(n - 1);
    return c >= top ? n - 1 : static_cast<uint32_t>(c);
  }
};

}  // namespace orbitfall
