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
}

void SpatialHash::queryNeighbors(float x, float y, float radius, std::vector<uint32_t>& outEntities) const {
    outEntities.clear();
    
    // Get center cell coordinates
    int32_t centerX = static_cast<int32_t>(x / cellSize);
    int32_t centerY = static_cast<int32_t>(y / cellSize);
    
    const float radiusSq = radius * radius;
    
    // Check 9 cells (3x3 grid) around center (Design Doc §5.4)
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            int32_t cellX = centerX + dx;
            int32_t cellY = centerY + dy;
            
            if (!isValidCell(cellX, cellY)) continue;
            
            uint32_t cellId = cellY * gridWidth + cellX;
            const auto& cell = cells[cellId];
            
            // Add all entities from this cell
            // (Could add distance filtering here, but caller typically does that)
            outEntities.insert(outEntities.end(), cell.begin(), cell.end());
        }
    }
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
