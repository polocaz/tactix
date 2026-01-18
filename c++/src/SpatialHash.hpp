#pragma once
#include <vector>
#include <cstdint>

// Spatial hash grid for efficient neighbor queries (Design Doc §5)
class SpatialHash {
public:
    SpatialHash(float worldWidth, float worldHeight, float cellSize);
    
    // Clear and rebuild the grid for current frame
    void clear();
    void insert(uint32_t entityId, float x, float y);
    
    // Query entities in 9-cell neighborhood (3x3 grid around position)
    void queryNeighbors(float x, float y, float radius, std::vector<uint32_t>& outEntities) const;
    
    // Debug info
    uint32_t getCellCount() const { return gridWidth * gridHeight; }
    uint32_t getMaxOccupancy() const;
    
    // Get cell coordinates for position
    void getCellCoords(float x, float y, int32_t& cellX, int32_t& cellY) const;

private:
    float cellSize;
    uint32_t gridWidth;
    uint32_t gridHeight;
    float worldWidth;
    float worldHeight;
    
    // Cell storage: vector of entity lists per cell
    std::vector<std::vector<uint32_t>> cells;
    
    // Hash position to cell ID (Design Doc §5.2)
    inline uint32_t hashPosition(float x, float y) const {
        int32_t cellX = static_cast<int32_t>(x / cellSize);
        int32_t cellY = static_cast<int32_t>(y / cellSize);
        
        // Clamp to grid bounds
        cellX = (cellX < 0) ? 0 : (cellX >= static_cast<int32_t>(gridWidth) ? gridWidth - 1 : cellX);
        cellY = (cellY < 0) ? 0 : (cellY >= static_cast<int32_t>(gridHeight) ? gridHeight - 1 : cellY);
        
        return cellY * gridWidth + cellX;
    }
    
    inline bool isValidCell(int32_t cellX, int32_t cellY) const {
        return cellX >= 0 && cellX < static_cast<int32_t>(gridWidth) &&
               cellY >= 0 && cellY < static_cast<int32_t>(gridHeight);
    }
};
