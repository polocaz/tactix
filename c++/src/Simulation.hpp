#pragma once
#include "platform.h"

#include <vector>
#include <cstdint>
#include <cmath>
#include "SpatialHash.hpp"
#include "JobSystem.hpp"
#include "Rng.hpp"
#include "WorkCounters.hpp"

// Structure of Arrays (SoA) for cache-friendly memory layout (Design Doc §2.1)
struct EntityHot {
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<float> dirX;  // Normalized direction for rendering
    std::vector<float> dirY;

    size_t count = 0;

    void reserve(size_t n) {
        posX.reserve(n);
        posY.reserve(n);
        velX.reserve(n);
        velY.reserve(n);
        dirX.reserve(n);
        dirY.reserve(n);
    }

    void spawn(float px, float py, float vx, float vy) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        const float speed = std::sqrt(vx * vx + vy * vy);
        if (speed > 0.01f) {
            dirX.push_back(vx / speed);
            dirY.push_back(vy / speed);
        } else {
            dirX.push_back(1.0f);
            dirY.push_back(0.0f);
        }
        count++;
    }
};

class Simulation {
public:
    Simulation(int screenWidth, int screenHeight, uint32_t seed = 1u, uint32_t workerThreads = 0u);

    void init(size_t count);
    void reset(size_t count);  // Tear down and re-init at a new agent count
    size_t getAgentCount() const { return entities.count; }
    uint32_t getSeed() const { return worldSeed; }
    void tick(float dt);  // Fixed timestep update (Design Doc §4)

    friend void drawSimulation(const Simulation& sim, float alpha);

    // Metrics access
    float getLastSpatialHashTime() const { return lastSpatialHashTime; }
    uint32_t getMaxCellOccupancy() const;
    bool isDebugGridEnabled() const { return debugGrid; }
    void toggleDebugGrid() { debugGrid = !debugGrid; }
    uint32_t getJobsExecuted() const { return jobSystem.getJobsExecuted(); }
    uint32_t getWorkerCount() const { return jobSystem.getWorkerCount(); }
    
    // Pause control
    bool isPaused() const { return paused; }
    void togglePause() { paused = !paused; }
    void setPaused(bool p) { paused = p; }

    // Bitwise hash of all simulation-visible state. See StateDigest.hpp.
    uint64_t stateDigest() const;

    // Deterministic, machine-independent measures of work done. See WorkCounters.hpp.
    const WorkCounters& counters() const { return workCounters; }
    void resetCounters() { workCounters.reset(); }

private:
    WorkCounters workCounters;

    int screenWidth;
    int screenHeight;

    uint32_t worldSeed = 1u;
    uint32_t tickNumber = 0u;

    EntityHot entities;  // Hot data (SoA)
    
    // Previous state for interpolation
    std::vector<float> prevPosX;
    std::vector<float> prevPosY;
    
    // Spatial partitioning (Phase 2)
    SpatialHash spatialHash;
    float lastSpatialHashTime = 0.0f;
    
    // Job system (Phase 3)
    JobSystem jobSystem;
    
    // Debug visualization
    bool debugGrid = false;
    bool paused = true;  // Start paused

    // Static obstacles for environment
    struct Building {
        float x, y, width, height;
    };
    struct Tree {
        float x, y, radius;
    };
    std::vector<Building> buildings;
    std::vector<Tree> trees;

    void generateObstacles();  // Procedural obstacle generation

    void updateMovement(float dt);
    void updateSeparation(float dt, const Rng& rng);  // Collision avoidance
    // Chunks take Rng BY VALUE: they run on worker threads via a lambda that
    // outlives the tick() local the Rng is constructed from.
    void updateSeparationChunk(size_t start, size_t end, float dt, Rng rng);  // Parallel version
    void updateMovementChunk(size_t start, size_t end, float dt);    // Parallel version (draws no randomness)
    void screenWrap();
    void rebuildSpatialHash();  // Rebuild spatial hash each tick
};
