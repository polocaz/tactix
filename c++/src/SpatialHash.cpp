#include "SpatialHash.hpp"
#include <cmath>
#include <algorithm>

SpatialHash::SpatialHash(float worldWidth, float worldHeight, float cellSize)
    : cellSize(cellSize)
    , worldWidth(worldWidth)
    , worldHeight(worldHeight)
{
    gridWidth = static_cast<uint32_t>(std::ceil(worldWidth / cellSize));
    gridHeight = static_cast<uint32_t>(std::ceil(worldHeight / cellSize));
    
    cells.resize(gridWidth * gridHeight);
}

void SpatialHash::clear() {
    // Clear all cell contents but keep allocated memory
    for (auto& cell : cells) {
        cell.clear();
    }
}

void SpatialHash::insert(uint32_t entityId, float x, float y) {
    uint32_t cellId = hashPosition(x, y);
    cells[cellId].push_back(entityId);
    if (counters) counters->add(counters->gridInsertions, 1);
}

// NOTE: `radius` is currently IGNORED. This always returns the fixed 3x3 cell block around
// (x, y) regardless of the requested radius, whether that block is larger or smaller than
// `radius` actually calls for. This is a known, pre-existing bug, deliberately not fixed here
// because doing so would change simulation behaviour/output. See the branch's final review notes.
//
// The hot phases call forEachNeighbor instead, which walks the same cells in the
// same order without materialising the result. This form remains for callers
// that genuinely want the list, and for the tests that pin the walk's contents.
void SpatialHash::queryNeighbors(float x, float y, float radius, std::vector<uint32_t>& outEntities) const {
    (void)radius;
    outEntities.clear();

    forEachCell(x, y, [&outEntities](const uint32_t* first, const uint32_t* last) {
        outEntities.insert(outEntities.end(), first, last);
        return true;
    });
}

uint32_t SpatialHash::getMaxOccupancy() const {
    uint32_t maxOccupancy = 0;
    for (const auto& cell : cells) {
        maxOccupancy = std::max(maxOccupancy, static_cast<uint32_t>(cell.size()));
    }
    return maxOccupancy;
}

void SpatialHash::getCellCoords(float x, float y, int32_t& cellX, int32_t& cellY) const {
    cellX = static_cast<int32_t>(x / cellSize);
    cellY = static_cast<int32_t>(y / cellSize);
    
    // Clamp to grid bounds
    cellX = (cellX < 0) ? 0 : (cellX >= static_cast<int32_t>(gridWidth) ? gridWidth - 1 : cellX);
    cellY = (cellY < 0) ? 0 : (cellY >= static_cast<int32_t>(gridHeight) ? gridHeight - 1 : cellY);
}
