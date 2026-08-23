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
#include "Loadout.hpp"
#include "Squads.hpp"
#include "Projectiles.hpp"
#include "Terrain.hpp"
#include "Army.hpp"

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
    // What this man is carrying, as a TroopClass byte. Weapon, armor and
    // shield are reached through kTroopLoadout rather than stored per soldier,
    // because they are read at RESOLUTION time (once per blow, low hundreds a
    // tick) and not once per soldier per tick. Two L1 table reads beat two
    // more bytes across 10,000 agents.
    std::vector<uint8_t>      troopClass;
    std::vector<SoldierState> state;
    std::vector<uint16_t>     squadId;
    std::vector<uint16_t>     slotIndex;
    std::vector<uint8_t>      health;
    std::vector<float>        attackCooldown;
    std::vector<uint32_t>     intentTarget;   // UINT32_MAX means none
    std::vector<uint8_t>      intentFire;

    // Seconds spent below a walking pace. Archery accuracy needs a settled
    // shooter, and this is what makes standing still worth something.
    std::vector<float>        steadyTimer;

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
        troopClass.reserve(n);
        state.reserve(n);
        squadId.reserve(n);
        slotIndex.reserve(n);
        health.reserve(n);
        attackCooldown.reserve(n);
        intentTarget.reserve(n);
        intentFire.reserve(n);
        steadyTimer.reserve(n);
    }

    void spawn(float px, float py, float vx, float vy, Team t, TroopClass tc, uint16_t squad) {
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

        // Unit type and starting health are DERIVED from the troop class, never
        // passed in: a caller cannot construct a soldier whose role and
        // equipment disagree.
        const Loadout& lo = loadoutOf(tc);
        team.push_back(t);
        unitType.push_back(lo.unit);
        troopClass.push_back((uint8_t)tc);
        state.push_back(SoldierState::Forming);
        squadId.push_back(squad);
        slotIndex.push_back(0);
        health.push_back(lo.maxHealth);
        attackCooldown.push_back(0.0f);
        intentTarget.push_back(std::numeric_limits<uint32_t>::max());
        intentFire.push_back(0);
        steadyTimer.push_back(0.0f);

        count++;
    }

    // Empties every array and resets the count. Lives HERE, next to spawn(),
    // and not as a hand-written list in Simulation::reset, because those two
    // lists have to agree field for field and nothing checks that they do.
    //
    // They did not agree: reset() set count to 0 while leaving the newer
    // arrays populated, so the next init() pushed onto them and every new
    // field came out offset by the previous run's count, silently reading the
    // last battle's values. Adding a field is now one edit here instead of a
    // memory-corruption bug waiting on someone remembering a second list.
    void clear() {
        posX.clear();
        posY.clear();
        velX.clear();
        velY.clear();
        dirX.clear();
        dirY.clear();
        team.clear();
        unitType.clear();
        troopClass.clear();
        state.clear();
        squadId.clear();
        slotIndex.clear();
        health.clear();
        attackCooldown.clear();
        intentTarget.clear();
        intentFire.clear();
        steadyTimer.clear();
        count = 0;
    }
};

// Where and how a man fell. Purely presentational: the renderer turns these
// into blood decals and impact flashes, and nothing in the simulation reads
// them back. Recording is opt-in (see Simulation::recordDeaths) so the
// headless benchmark never pays for a feature only the window uses.
struct DeathEvent {
    float    x, y;
    Team     team;
    UnitType unit;
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
    uint8_t  soldierTroopClass(size_t i) const { return soldiers.troopClass[i]; }
    uint32_t soldierIntentTarget(size_t i) const { return soldiers.intentTarget[i]; }
    uint8_t  soldierHealth(size_t i) const { return soldiers.health[i]; }
    bool   everySoldierHasASquadSlot() const;

    // Per-squad accessors (Task 8). Used to check a squad's centroid does
    // not drift over time with no orders given.
    float  squadCentroidX(size_t s) const { return squads.centroidX[s]; }
    float  squadCentroidY(size_t s) const { return squads.centroidY[s]; }
    Team   squadTeam(size_t s) const { return squads.team[s]; }
    uint16_t squadTargetSquad(size_t s) const { return squads.targetSquad[s]; }
    uint8_t  squadContact(size_t s) const { return squads.contact[s]; }
    float    squadMorale(size_t s) const { return squads.morale[s]; }
    float    squadDiscipline(size_t s) const { return squads.discipline[s]; }
    float    squadNearestEnemyDist(size_t s) const { return squads.nearestEnemyDist[s]; }
    UnitType squadUnitType(size_t s) const { return squads.unitType[s]; }
    uint8_t  squadTroopClass(size_t s) const { return squads.troopClass[s]; }
    uint8_t  squadOrder(size_t s) const { return squads.order[s]; }
    uint8_t  squadRole(size_t s) const { return squads.role[s]; }
    float    armyCentroidX(Team t) const { return armies.centroidX[(size_t)t]; }
    float    armyCentroidY(Team t) const { return armies.centroidY[(size_t)t]; }
    uint16_t squadWardSquad(size_t s) const { return squads.wardSquad[s]; }
    float    squadObjectiveX(size_t s) const { return squads.objectiveX[s]; }
    float    squadObjectiveY(size_t s) const { return squads.objectiveY[s]; }
    uint16_t soldierSquadId(size_t i) const { return soldiers.squadId[i]; }
    float    soldierDirX(size_t i) const { return soldiers.dirX[i]; }
    float    soldierDirY(size_t i) const { return soldiers.dirY[i]; }
    float    squadFacingX(size_t s) const { return squads.facingX[s]; }
    float    squadFacingY(size_t s) const { return squads.facingY[s]; }
    float    soldierSteadyTimer(size_t i) const { return soldiers.steadyTimer[i]; }
    float    soldierSpeed(size_t i) const {
        return std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                         soldiers.velY[i] * soldiers.velY[i]);
    }
    // Only meaningful mid-tick, between phase 2 (where it is computed) and
    // resolution step 3 (where spawnArrows consumes it) -- phaseResolution
    // clears it to UINT32_MAX once compaction can have invalidated it, so a
    // caller reading this between ticks always sees UINT32_MAX, never a
    // stale post-compaction index.
    uint32_t squadTargetSoldier(size_t s) const { return squads.targetSoldier[s]; }

    // Nearest point to p that a soldier can actually stand on: clear of
    // every building and tree by kObstacleStandoff. See TerrainField for the
    // definition and why formation slots are routed through it. Public here
    // as a thin delegating wrapper so existing callers and tests are
    // unchanged (design §4.1: Simulation owns generation, terrain owns
    // geometry).
    Vec2 clearOfObstacles(Vec2 p) const { return terrain.clearOfObstacles(p); }

    // True if p is inside any building or tree. Exposed so a test can state
    // the property clearOfObstacles establishes.
    bool insideAnyObstacle(Vec2 p) const { return terrain.insideAnyObstacle(p); }

    // Distance from one soldier to its own formation slot (Task 8).
    float  slotError(size_t i) const;

    // Mean distance from each soldier to its assigned formation slot
    // (Task 8). Used by tests to check steering converges over time.
    float  meanSlotError() const;

    friend void drawSimulation(const Simulation& sim, float alpha,
                               const struct ViewSettings& view);
    friend void drawHud(const Simulation& sim, const struct ViewSettings& view,
                        float timeScale, bool paused, float battleSeconds);
    friend void renderInit(const Simulation& sim);

    // Presentation-only death log. Off by default: turning it on costs one
    // extra pass over the soldier array per tick, which the window can afford
    // and the benchmark should not be charged for.
    bool recordDeaths = false;
    const std::vector<DeathEvent>& deathEvents() const { return deaths; }
    void clearDeathEvents() { deaths.clear(); }

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

    // The army tier: exactly two entries, one per team. Never destroyed, so
    // nothing reading an army index needs a liveness check.
    ArmyHot armies;
    std::vector<uint32_t> squadMembers;
    // Scratch for rebuildSquadMembers, owned here so the once-a-tick serial
    // call reuses this capacity instead of heap-allocating every tick.
    std::vector<uint32_t> squadMemberCounts;
    std::vector<uint32_t> squadMemberCursor;

    // Per-squad scratch, cleared at the start of every resolution phase. Plan 3
    // consumes both to drive morale and the officer-death discipline penalty.
    std::vector<uint32_t> casualties;
    std::vector<uint8_t>  officerDied;

    // Projectiles in flight
    ProjectileHot projectiles;

    // Filled during resolution when recordDeaths is set, drained by the
    // renderer each frame. Bounded so a viewer who never drains it (or a very
    // long unattended run) cannot grow it without limit.
    std::vector<DeathEvent> deaths;
    static constexpr size_t kMaxRecordedDeaths = 4096;

    // Previous state for interpolation
    std::vector<float> prevPosX;
    std::vector<float> prevPosY;

    // Integrated positions, written by phase 8 and consumed by phase 9. The
    // split exists so non-penetration reads a consistent read-only snapshot
    // rather than positions other jobs are concurrently updating.
    std::vector<float> nextPosX;
    std::vector<float> nextPosY;
    
    // Spatial partitioning (Phase 2)
    SpatialHash spatialHash;
    float lastSpatialHashTime = 0.0f;
    
    // Job system (Phase 3)
    JobSystem jobSystem;
    
    // Debug visualization
    bool debugGrid = false;
    bool paused = true;  // Start paused

    // Static obstacles for environment. Geometry lives on TerrainField; this
    // is the Simulation-owned instance (design §4.1). generateObstacles
    // populates it; per-soldier collision and squad terrain scoring read it.
    TerrainField terrain;

    void generateObstacles();  // Procedural obstacle generation

    // Tick phases, in the order Simulation::tick calls them (Design Doc §4).
    void rebuildSpatialHash();  // Rebuild spatial hash each tick
    void rebuildInfluence();    // Stub: plan 3 fills this in.
    void phaseSquadAggregate(float dt);       // Plan 7: recomputes each squad's centroid and facing, parallel across squads.
    void phaseArmyDecide();                  // Phase 3: serial, 2 entities.
    void phaseSquadDecide(float dt, const Rng& rng);  // Phase 4: parallel over squads.
    void phaseSoldierSteer(float dt, const Rng& rng);  // Collision avoidance
    // Chunks take Rng BY VALUE: they run on worker threads via a lambda that
    // outlives the tick() local the Rng is constructed from.
    void phaseSoldierSteerChunk(size_t start, size_t end, float dt, Rng rng);  // Parallel version
    void phaseProjectiles(float dt);  // Phase 5: integrate arrows and hit-test.
    // Phase 6 (spec 5.5). Serial; the only place cross-agent mutation is
    // permitted anywhere in the tick. Runs melee, then projectile hits, then
    // spawns this tick's arrows (which consumes targetSoldier), then records
    // casualties, then compacts projectiles and dead soldiers, then rebuilds
    // squad membership from the survivors, then clears targetSoldier now
    // that compaction can have made it stale. The order is load-bearing:
    // each step depends on the ones before it and would corrupt or misread
    // data if reordered -- see the step-numbered comments in the .cpp.
    void phaseResolution(const Rng& rng);
    void phaseMovement(float dt);
    void phaseMovementChunk(size_t start, size_t end, float dt);    // Parallel version (draws no randomness)
    void phaseContact();                                            // Phase 9: non-penetration
    void phaseContactChunk(size_t start, size_t end);               // Parallel version
    void clampToWorld();  // Clamps positions to world bounds and bounces velocity (never wraps, despite older code's name for this)
};
