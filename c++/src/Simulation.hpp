#pragma once
#include "platform.h"

#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include "SpatialHash.hpp"
#include "JobSystem.hpp"
#include "Rng.hpp"
#include "WorkCounters.hpp"
#include "Units.hpp"
#include "Squads.hpp"
#include "Projectiles.hpp"

// Structure of Arrays (SoA) for cache-friendly memory layout (Design Doc §2.1)
struct SoldierHot {
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<float> dirX;  // Normalized direction for rendering
    std::vector<float> dirY;

    std::vector<Team>         team;
    std::vector<UnitType>     unitType;
    std::vector<SoldierState> state;
    std::vector<uint16_t>     squadId;
    std::vector<uint16_t>     slotIndex;
    std::vector<uint8_t>      health;
    std::vector<float>        attackCooldown;
    std::vector<uint32_t>     intentTarget;   // UINT32_MAX means none
    std::vector<uint8_t>      intentFire;

    size_t count = 0;

    void reserve(size_t n) {
        posX.reserve(n);
        posY.reserve(n);
        velX.reserve(n);
        velY.reserve(n);
        dirX.reserve(n);
        dirY.reserve(n);

        team.reserve(n);
        unitType.reserve(n);
        state.reserve(n);
        squadId.reserve(n);
        slotIndex.reserve(n);
        health.reserve(n);
        attackCooldown.reserve(n);
        intentTarget.reserve(n);
        intentFire.reserve(n);
    }

    void spawn(float px, float py, float vx, float vy, Team t, UnitType ut, uint16_t squad) {
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

        team.push_back(t);
        unitType.push_back(ut);
        state.push_back(SoldierState::Forming);
        squadId.push_back(squad);
        slotIndex.push_back(0);
        health.push_back(kUnitStats[(int)ut].maxHealth);
        attackCooldown.push_back(0.0f);
        intentTarget.push_back(std::numeric_limits<uint32_t>::max());
        intentFire.push_back(0);

        count++;
    }
};

class Simulation {
public:
    Simulation(int worldWidth, int worldHeight, uint32_t seed = 1u, uint32_t workerThreads = 0u);

    void init(size_t count);
    void reset(size_t count);  // Tear down and re-init at a new agent count
    size_t getAgentCount() const { return soldiers.count; }
    uint32_t getSeed() const { return worldSeed; }
    void tick(float dt);  // Fixed timestep update (Design Doc §4)

    // Deployment / squad accessors (Task 5)
    size_t getSquadCount() const { return squads.count; }
    size_t getProjectileCount() const { return projectiles.count; }
    size_t getTeamCount(Team t) const;
    float  teamCentroidX(Team t) const;
    float  soldierX(size_t i) const { return soldiers.posX[i]; }
    float  soldierY(size_t i) const { return soldiers.posY[i]; }
    Team   soldierTeam(size_t i) const { return soldiers.team[i]; }
    UnitType soldierUnitType(size_t i) const { return soldiers.unitType[i]; }
    uint32_t soldierIntentTarget(size_t i) const { return soldiers.intentTarget[i]; }
    uint8_t  soldierHealth(size_t i) const { return soldiers.health[i]; }
    bool   everySoldierHasASquadSlot() const;

    // Per-squad accessors (Task 8). Used to check a squad's centroid does
    // not drift over time with no orders given.
    float  squadCentroidX(size_t s) const { return squads.centroidX[s]; }
    float  squadCentroidY(size_t s) const { return squads.centroidY[s]; }
    Team   squadTeam(size_t s) const { return squads.team[s]; }
    uint16_t squadTargetSquad(size_t s) const { return squads.targetSquad[s]; }

    // Distance from one soldier to its own formation slot (Task 8).
    float  slotError(size_t i) const;

    // Mean distance from each soldier to its assigned formation slot
    // (Task 8). Used by tests to check steering converges over time.
    float  meanSlotError() const;

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

    int worldWidth;
    int worldHeight;

    uint32_t worldSeed = 1u;
    uint32_t tickNumber = 0u;

    SoldierHot soldiers;  // Hot data (SoA)

    // Squad tier: per-squad aggregate data and the per-tick membership index.
    SquadHot squads;
    std::vector<uint32_t> squadMembers;

    // Projectiles in flight
    ProjectileHot projectiles;

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

    // Tick phases, in the order Simulation::tick calls them (Design Doc §4).
    void rebuildSpatialHash();  // Rebuild spatial hash each tick
    void rebuildInfluence();    // Stub: plan 3 fills this in.
    void phaseSquadAggregate();       // Plan 7: recomputes each squad's centroid and facing, parallel across squads.
    void phaseSquadDecide(const Rng& rng);  // Stub: plan 3 fills this in.
    void phaseSoldierSteer(float dt, const Rng& rng);  // Collision avoidance
    // Chunks take Rng BY VALUE: they run on worker threads via a lambda that
    // outlives the tick() local the Rng is constructed from.
    void phaseSoldierSteerChunk(size_t start, size_t end, float dt, Rng rng);  // Parallel version
    void phaseProjectiles(float dt);  // Stub: plan 2 fills this in.
    void phaseResolution(const Rng& rng);  // Stub: plan 2 fills this in. Only place cross-agent mutation is permitted.
    void phaseMovement(float dt);
    void phaseMovementChunk(size_t start, size_t end, float dt);    // Parallel version (draws no randomness)
    void clampToWorld();  // Clamps positions to world bounds and bounces velocity (never wraps, despite older code's name for this)
};
