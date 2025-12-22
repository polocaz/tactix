#pragma once
#include <vector>
#include <cstdint>
#include "SpatialHash.hpp"

// Structure of Arrays (SoA) for cache-friendly memory layout (Design Doc §2.1)
struct EntityHot {
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<uint8_t> state;  // Future: entity state
    
    size_t count = 0;
    
    void reserve(size_t n) {
        posX.reserve(n);
        posY.reserve(n);
        velX.reserve(n);
        velY.reserve(n);
        state.reserve(n);
    }
    
    void spawn(float px, float py, float vx, float vy) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        state.push_back(0);
        count++;
    }
};

class Simulation {
public:
    Simulation(int screenWidth, int screenHeight);

    void init(size_t count);
    void tick(float dt);  // Fixed timestep update (Design Doc §4)
    void draw(float alpha);  // Interpolated rendering (Design Doc §8.1)
    
    // Metrics access
    float getLastSpatialHashTime() const { return lastSpatialHashTime; }
    uint32_t getMaxCellOccupancy() const;
    bool isDebugGridEnabled() const { return debugGrid; }
    void toggleDebugGrid() { debugGrid = !debugGrid; }

private:
    int screenWidth;
    int screenHeight;

    EntityHot entities;  // Hot data (SoA)
    
    // Previous state for interpolation
    std::vector<float> prevPosX;
    std::vector<float> prevPosY;
    
    // Spatial partitioning (Phase 2)
    SpatialHash spatialHash;
    float lastSpatialHashTime = 0.0f;
    
    // Neighbor query temp buffer (reused to avoid allocations)
    mutable std::vector<uint32_t> neighborBuffer;
    
    // Debug visualization
    bool debugGrid = false;

    void updateMovement(float dt);
    void updateSeparation(float dt);  // Collision avoidance
    void screenWrap();
    void rebuildSpatialHash();  // Rebuild spatial hash each tick
};
