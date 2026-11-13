#include "spatial/grid.h"

namespace orbitfall {

void SpatialGrid::configure(float halfExtent, uint32_t cellsPerSide, uint32_t capacity) {
  if (cellsPerSide == 0) cellsPerSide = 1;
  if (!(halfExtent > 0.0f)) halfExtent = 1.0f;
  minX = -halfExtent;
  minY = -halfExtent;
  cellSize = 2.0f * halfExtent / static_cast<float>(cellsPerSide);
  invCellSize = 1.0f / cellSize;
  cols = cellsPerSide;
  rows = cellsPerSide;
  count = 0;
  cellStart.assign(static_cast<size_t>(cols) * rows + 1, 0u);
  items.assign(capacity, 0u);
  cellOf.assign(capacity, 0u);
}

void SpatialGrid::rebuild(const float* x, const float* y, uint32_t n) {
  if (cols == 0) return;
  if (n > items.size()) n = static_cast<uint32_t>(items.size());
  count = n;
  const uint32_t cells = cols * rows;
  for (uint32_t c = 0; c <= cells; ++c) cellStart[c] = 0;
  // Count per cell (shifted by one), prefix-sum, then place.
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t cell = cellY(y[i]) * cols + cellX(x[i]);
    cellOf[i] = cell;
    ++cellStart[cell + 1];
  }
  for (uint32_t c = 0; c < cells; ++c) cellStart[c + 1] += cellStart[c];
  // Place using cellStart as the write cursor, then shift it back.
  for (uint32_t i = 0; i < n; ++i) items[cellStart[cellOf[i]]++] = i;
  for (uint32_t c = cells; c > 0; --c) cellStart[c] = cellStart[c - 1];
  cellStart[0] = 0;
}

}  // namespace orbitfall
