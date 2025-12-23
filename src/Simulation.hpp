#pragma once
#include "platform.h"

#include <vector>
#include <cstdint>
#include "SpatialHash.hpp"
#include "JobSystem.hpp"
#include <raylib.h>

// Agent types for zombie simulation
enum class AgentType : uint8_t {
    Civilian = 0,
    Zombie = 1,
    Hero = 2
};

// AI states for behavior system
enum class AgentState : uint8_t {
    Idle = 0,      // Just standing/minimal movement
    Patrol = 1,    // Wandering to random destinations
    Fleeing = 2,   // Running away from threat
    Pursuing = 3,  // Chasing target
    Searching = 4  // Looking for last known target location
};

// Structure of Arrays (SoA) for cache-friendly memory layout (Design Doc §2.1)
struct EntityHot {
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<float> dirX;  // Normalized direction for rendering
    std::vector<float> dirY;
    std::vector<AgentType> type;  // Agent type
    std::vector<AgentState> state;  // Current AI state
    std::vector<uint8_t> health;  // Hero health (kills remaining), unused for others
    
    // Memory system for persistent behavior
    std::vector<float> lastSeenX;  // Last known target position
    std::vector<float> lastSeenY;
    std::vector<float> searchTimer;  // Time spent searching
    std::vector<float> patrolTargetX;  // Patrol destination
    std::vector<float> patrolTargetY;
    
    size_t count = 0;
    
    void reserve(size_t n) {
        posX.reserve(n);
        posY.reserve(n);
        velX.reserve(n);
        velY.reserve(n);
        dirX.reserve(n);
        dirY.reserve(n);
        type.reserve(n);
        state.reserve(n);
        health.reserve(n);
        lastSeenX.reserve(n);
        lastSeenY.reserve(n);
        searchTimer.reserve(n);
    }
    
    void spawn(float px, float py, float vx, float vy, AgentType agentType) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        // Initial direction from velocity
        float speed = std::sqrt(vx * vx + vy * vy);
        if (speed > 0.01f) {
            dirX.push_back(vx / speed);
            dirY.push_back(vy / speed);
        } else {
            dirX.push_back(1.0f);  // Default facing right
            dirY.push_back(0.0f);
        }
        type.push_back(agentType);
        state.push_back(AgentState::Patrol);  // Start patrolling
        health.push_back(agentType == AgentType::Hero ? 5 : 0);  // Heroes start with 5 kills
        lastSeenX.push_back(0.0f);
        lastSeenY.push_back(0.0f);
        searchTimer.push_back(0.0f);
        // Random initial patrol target
        patrolTargetX.push_back((float)GetRandomValue(50, 1850));
        patrolTargetY.push_back((float)GetRandomValue(50, 1030));
        count++;
    }
};

class Simulation {
public:
    Simulation(int screenWidth, int screenHeight);

    void init(size_t count);
    void setAgentCount(size_t count);  // Dynamically adjust agent count
    size_t getAgentCount() const { return entities.count; }
    void tick(float dt);  // Fixed timestep update (Design Doc §4)
    void draw(float alpha);  // Interpolated rendering (Design Doc §8.1)
    
    // Metrics access
    float getLastSpatialHashTime() const { return lastSpatialHashTime; }
    uint32_t getMaxCellOccupancy() const;
    bool isDebugGridEnabled() const { return debugGrid; }
    void toggleDebugGrid() { debugGrid = !debugGrid; }
    uint32_t getJobsExecuted() const { return jobSystem.getJobsExecuted(); }
    uint32_t getWorkerCount() const { return jobSystem.getWorkerCount(); }
    
    // Agent type counts
    size_t getCivilianCount() const;
    size_t getZombieCount() const;
    size_t getHeroCount() const;

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
    
    // Job system (Phase 3)
    JobSystem jobSystem;
    
    // Neighbor query temp buffer (reused to avoid allocations)
    mutable std::vector<uint32_t> neighborBuffer;
    
    // Debug visualization
    bool debugGrid = false;

    void updateMovement(float dt);
    void updateSeparation(float dt);  // Collision avoidance
    void updateBehaviors(float dt);   // Seek/flee/combat behaviors
    void updateInfections();          // Handle zombie infections
    void updateSeparationChunk(size_t start, size_t end, float dt);  // Parallel version
    void updateMovementChunk(size_t start, size_t end, float dt);    // Parallel version
    void updateBehaviorsChunk(size_t start, size_t end, float dt);   // Parallel version
    void screenWrap();
    void rebuildSpatialHash();  // Rebuild spatial hash each tick
};
