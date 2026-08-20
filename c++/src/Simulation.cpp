#include "platform.h"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Soldiers.hpp"
#include "Formation.hpp"
#include "StateDigest.hpp"
#include "DetMath.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include "spdlog/spdlog.h"

Simulation::Simulation(int w, int h, uint32_t seed, uint32_t workerThreads)
    : worldWidth(w), worldHeight(h)
    , worldSeed(seed)
    , spatialHash(static_cast<float>(w), static_cast<float>(h), 50.0f)  // 50 pixel cells (Design Doc §5.1)
    , jobSystem(workerThreads)
{
    spatialHash.setCounters(&workCounters);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void Simulation::init(size_t soldierCount) {
    spdlog::info("Initializing {} agents", soldierCount);
    // init() runs before any tick, so tick 0 is reserved for setup draws.
    const Rng rng{worldSeed, 0u};
    soldiers.reserve(soldierCount);
    prevPosX.reserve(soldierCount);
    prevPosY.reserve(soldierCount);

    generateObstacles();

    // Squad composition by count: 60% infantry, 25% archers, 15% cavalry.
    constexpr uint32_t kSquadSize = 25;
    // Team A gets the floor half, team B the remainder, so an odd
    // soldierCount still deploys every soldier requested (off by one
    // between the two armies rather than one soldier short overall).
    const size_t perTeamA = soldierCount / 2;

    const float w = (float)worldWidth;
    const float h = (float)worldHeight;

    // Rows stack a team's squads down the field; columns wrap a second rank
    // in behind the first once a column fills up. Both pitches are derived
    // from real quantities (a squad's own formation footprint, and the
    // depth actually available behind the front line) rather than fixed
    // constants, so neither can push a squad origin outside the world. If
    // the requested density does not fit at those pitches, both pack
    // tighter instead of spilling past the world edge -- see the warning
    // below.
    bool packedTighter = false;

    for (int t = 0; t < 2; ++t) {
        const Team team = (t == 0) ? Team::A : Team::B;
        const size_t perTeam = (t == 0) ? perTeamA : (soldierCount - perTeamA);
        const uint32_t squadsPerTeam = (uint32_t)((perTeam + kSquadSize - 1) / kSquadSize);
        if (squadsPerTeam == 0) continue;

        // Team A faces right from the left margin, team B faces left.
        const float baseX = (t == 0) ? w * 0.15f : w * 0.85f;
        const float facing = (t == 0) ? 1.0f : -1.0f;
        // Depth behind the front line before the corridor runs into the
        // world edge -- exactly the distance from baseX to that edge, so a
        // column offset (below) can never carry a squad past it.
        const float colBandDepth = (t == 0) ? baseX : (w - baseX);
        // Vertical band squads stack down: h*0.1 to h*0.9.
        const float rowBand = h * 0.8f;

        // Pass 1: every squad's shape and member count is knowable without
        // touching a soldier, so scan them first for the largest formation
        // footprint (Formation.hpp's formationExtent) this team will
        // actually deploy. That sets the spacing floor neighbouring squads
        // need so they do not overlap (finding 2). Floored at kSlotSpacing
        // so a pitch is never zero regardless of member counts.
        float neededRowPitch = kSlotSpacing;
        float neededColPitch = kSlotSpacing;
        for (uint32_t sq = 0; sq < squadsPerTeam; ++sq) {
            UnitType unit = UnitType::Infantry;
            const uint32_t bucket = sq % 20;
            if (bucket >= 12 && bucket < 17)      unit = UnitType::Archer;
            else if (bucket >= 17)                unit = UnitType::Cavalry;
            const uint32_t members = (uint32_t)std::min<size_t>(
                kSquadSize, perTeam - (size_t)sq * kSquadSize);
            const Vec2 extent = formationExtent(shapeForUnit(unit), members);
            neededRowPitch = std::max(neededRowPitch, extent.x);
            neededColPitch = std::max(neededColPitch, extent.y);
        }

        const uint32_t perColumn = std::max(1u, (uint32_t)(rowBand / neededRowPitch));
        const uint32_t columnsNeeded = (squadsPerTeam + perColumn - 1) / perColumn;
        const float rowPitch = rowBand / (float)perColumn;
        const float colPitch = colBandDepth / (float)columnsNeeded;
        // Both loops above only ever shrink a pitch relative to what the
        // squads actually need (division against a fixed band), never grow
        // it, so "less than needed" is exactly the degrade case (finding 1).
        if (rowPitch < neededRowPitch || colPitch < neededColPitch) {
            packedTighter = true;
        }

        // Pass 2: place squads and spawn their soldiers.
        for (uint32_t sq = 0; sq < squadsPerTeam; ++sq) {
            UnitType unit = UnitType::Infantry;
            const uint32_t bucket = sq % 20;
            if (bucket >= 12 && bucket < 17)      unit = UnitType::Archer;
            else if (bucket >= 17)                unit = UnitType::Cavalry;

            const uint16_t squadId = (uint16_t)squads.count;
            squads.spawn(team, unit);
            squads.facingX[squadId] = facing;
            squads.facingY[squadId] = 0.0f;
            // Established here, not assumed: slotWorldPosition below (and
            // every steerToSlot call this tick and after) depends on facing
            // being unit length (see Soldiers.hpp).
            normalizeFacing(squads, squadId);

            const uint32_t column = sq / perColumn;
            const uint32_t row = sq % perColumn;
            const float squadX = baseX - facing * (float)column * colPitch;
            const float squadY = h * 0.1f + (float)row * rowPitch;

            squads.centroidX[squadId] = squadX;
            squads.centroidY[squadId] = squadY;

            // sq < squadsPerTeam = ceil(perTeam / kSquadSize), so by the
            // definition of ceiling division sq * kSquadSize < perTeam here:
            // the subtraction below never underflows.
            const uint32_t members = (uint32_t)std::min<size_t>(
                kSquadSize, perTeam - (size_t)sq * kSquadSize);
            // steerToSlot reads squads.memberCount, not a local variable --
            // set the field itself so deployment and steering agree by
            // construction, not by coincidence (finding 4).
            squads.memberCount[squadId] = members;

            for (uint32_t k = 0; k < members; ++k) {
                // Spawn exactly on the slot steerToSlot will target, using the
                // same rotation (slotWorldPosition), so tick 1 moves nobody.
                // Squad centroid/facing above must be set before this call.
                const uint32_t agent = (uint32_t)soldiers.count;
                const float jx = (float)rng.range(agent, RngUse::DeployJitterX, -2, 2);
                const float jy = (float)rng.range(agent, RngUse::DeployJitterY, -2, 2);
                const Vec2 slot = slotWorldPosition(squads, squadId, (uint16_t)k,
                                                    squads.memberCount[squadId]);
                const float px = slot.x + jx;
                const float py = slot.y + jy;

                soldiers.spawn(clampf(px, 0.0f, w), clampf(py, 0.0f, h),
                               0.0f, 0.0f, team, unit, squadId);
                soldiers.slotIndex[agent] = (uint16_t)k;
                // A soldier that never moves keeps the velocity-derived
                // direction SoldierHot::spawn defaults to, (1,0), forever --
                // seed it from the squad's own facing instead so a
                // stationary soldier still renders facing its own front
                // (finding 3). phaseMovementChunk's speed-gated update
                // still owns direction once a soldier actually moves.
                soldiers.dirX[agent] = squads.facingX[squadId];
                soldiers.dirY[agent] = squads.facingY[squadId];
                prevPosX.push_back(soldiers.posX[agent]);
                prevPosY.push_back(soldiers.posY[agent]);
            }
        }
    }

    if (packedTighter) {
        spdlog::warn("Deployment denser than formation spacing wants: {} agents on a {}x{} "
                     "field -- squads packed tighter than their own formation extent",
                     soldierCount, worldWidth, worldHeight);
    }

    // Populate memberCount/memberStart (otherwise left at zero until the
    // first tick) so slotWorldPosition, used both above and by tests that
    // check placement immediately after init, sees each squad's real size.
    // Deployment already assigned distinct slotIndex 0..members-1 per squad,
    // so this reproduces exactly what the first tick would compute anyway.
    rebuildSquadMembers(soldiers, squads, squadMembers);

    spdlog::info("Deployed {} soldiers in {} squads on a {}x{} field",
                 soldiers.count, squads.count, worldWidth, worldHeight);
}

size_t Simulation::getTeamCount(Team t) const {
    size_t n = 0;
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.team[i] == t) n++;
    }
    return n;
}

float Simulation::teamCentroidX(Team t) const {
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.team[i] != t) continue;
        sum += soldiers.posX[i];
        n++;
    }
    return n ? (float)(sum / (double)n) : 0.0f;
}

float Simulation::slotError(size_t i) const {
    const uint16_t s = soldiers.squadId[i];
    const Vec2 t = slotWorldPosition(squads, s, soldiers.slotIndex[i],
                                     squads.memberCount[s]);
    const float dx = t.x - soldiers.posX[i];
    const float dy = t.y - soldiers.posY[i];
    return std::sqrt(dx * dx + dy * dy);
}

float Simulation::meanSlotError() const {
    double sum = 0.0;
    for (size_t i = 0; i < soldiers.count; ++i) {
        sum += slotError(i);
    }
    return soldiers.count ? (float)(sum / (double)soldiers.count) : 0.0f;
}

bool Simulation::everySoldierHasASquadSlot() const {
    for (size_t i = 0; i < soldiers.count; ++i) {
        const uint16_t s = soldiers.squadId[i];
        if (s >= squads.count) return false;
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        if (soldiers.slotIndex[i] >= n) return false;
        if (squadMembers[start + soldiers.slotIndex[i]] != i) return false;
    }
    return true;
}

void Simulation::reset(size_t count) {
    soldiers.posX.clear();
    soldiers.posY.clear();
    soldiers.velX.clear();
    soldiers.velY.clear();
    soldiers.dirX.clear();
    soldiers.dirY.clear();
    soldiers.team.clear();
    soldiers.unitType.clear();
    soldiers.state.clear();
    soldiers.squadId.clear();
    soldiers.slotIndex.clear();
    soldiers.health.clear();
    soldiers.attackCooldown.clear();
    soldiers.intentTarget.clear();
    soldiers.intentFire.clear();
    soldiers.count = 0;

    squads.team.clear();
    squads.unitType.clear();
    squads.centroidX.clear();
    squads.centroidY.clear();
    squads.facingX.clear();
    squads.facingY.clear();
    squads.order.clear();
    squads.targetSquad.clear();
    squads.targetSoldier.clear();
    squads.morale.clear();
    squads.discipline.clear();
    squads.memberStart.clear();
    squads.memberCount.clear();
    squads.count = 0;
    squadMembers.clear();

    prevPosX.clear();
    prevPosY.clear();

    buildings.clear();
    trees.clear();

    tickNumber = 0;

    init(count);
}

void Simulation::generateObstacles() {
    // Runs before any tick; no agent involved, so the obstacle index keys the draw.
    const Rng rng{worldSeed, 0u};

    // City blocks (buildings)
    const int blockCount = 8;
    for (int i = 0; i < blockCount; i++) {
        float x = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingX, 100, worldWidth - 200);
        float y = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingY, 100, worldHeight - 200);
        float w = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingW, 80, 150);
        float h = (float)rng.range((uint32_t)i, RngUse::ObstacleBuildingH, 80, 150);
        buildings.push_back({x, y, w, h});
    }

    // Scattered trees
    const int treeCount = 30;
    for (int i = 0; i < treeCount; i++) {
        float x = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeX, 50, worldWidth - 50);
        float y = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeY, 50, worldHeight - 50);
        float r = (float)rng.range((uint32_t)i, RngUse::ObstacleTreeRadius, 15, 25);
        trees.push_back({x, y, r});
    }

    spdlog::info("Generated {} buildings and {} trees", buildings.size(), trees.size());
}

void Simulation::tick(float dt) {
    if (paused) return;

    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};

    for (size_t i = 0; i < soldiers.count; i++) {
        prevPosX[i] = soldiers.posX[i];
        prevPosY[i] = soldiers.posY[i];
    }

    jobSystem.resetJobCounter();

    // tick() is the single owner of every barrier between phases: each
    // parallel phase below submits jobs and returns without waiting, and
    // the jobSystem.waitAll() immediately after it is the only barrier for
    // that phase. Do not add another inside a phase function -- a phase
    // that carries its own barrier is indistinguishable from here, but the
    // next plan that copies its structure without noticing the extra
    // barrier is the one whose omission breaks silently.
    //
    // Phase 1: serial. The influence grid accumulates floats, and summing
    // them in index order on one thread is what makes the result
    // bit-reproducible. Atomics from workers would not be.
    rebuildSpatialHash();
    rebuildInfluence();
    rebuildSquadMembers(soldiers, squads, squadMembers);

    // Phase 2: parallel over squads. Writes only its own squad.
    phaseSquadAggregate();
    jobSystem.waitAll();

    // Phase 3: parallel over squads. Safe to read every squad's aggregate
    // only because the barrier above made those values read-only.
    phaseSquadDecide(rng);
    jobSystem.waitAll();

    // Phase 4: parallel over soldiers. Writes only its own soldier.
    phaseSoldierSteer(dt, rng);
    jobSystem.waitAll();

    // Phase 5: parallel over projectiles.
    phaseProjectiles(dt);
    jobSystem.waitAll();

    // Phase 6: serial. The ONLY place cross-agent mutation happens.
    phaseResolution(rng);

    // Phase 7: parallel over soldiers.
    phaseMovement(dt);
    jobSystem.waitAll();

    clampToWorld();
}

void Simulation::rebuildSpatialHash() {
    auto start = std::chrono::steady_clock::now();

    spatialHash.clear();
    for (size_t i = 0; i < soldiers.count; i++) {
        spatialHash.insert(static_cast<uint32_t>(i), soldiers.posX[i], soldiers.posY[i]);
    }

    auto end = std::chrono::steady_clock::now();
    lastSpatialHashTime = std::chrono::duration<float>(end - start).count() * 1000.0f;  // ms
}

void Simulation::rebuildInfluence() {
    // Plan 3 fills this in. Declared here so the phase order is visible and
    // fixed from the start rather than being inserted later.
}

void Simulation::phaseSquadAggregate() {
    // Plan 7: recompute each squad's centroid and facing. Parallel across
    // squads, never within one -- updateSquadAggregate sums one squad's
    // members on a single thread so the accumulation order is fixed.
    const size_t chunkSize = 32;
    for (size_t start = 0; start < squads.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, squads.count);
        jobSystem.submit([this, start, end]() {
            for (size_t s = start; s < end; ++s) {
                updateSquadAggregate(soldiers, squads, squadMembers, s);
                workCounters.add(workCounters.squadDecisions, 1);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}

void Simulation::phaseSquadDecide(const Rng&) {
    // Plan 3 fills this in. Every squad currently holds the Advance order it
    // was deployed with.
}

void Simulation::phaseProjectiles(float) {
    // Plan 2 fills this in.
}

void Simulation::phaseResolution(const Rng&) {
    // Plan 2 fills this in. Kept in the phase order now because it is the
    // only place cross-agent mutation is permitted, and later plans must not
    // be tempted to put that anywhere else.
}

void Simulation::phaseSoldierSteer(float dt, const Rng& rng) {
    // Parallelize collision avoidance (Design Doc §6.2)
    const size_t chunkSize = 256;  // Job granularity

    for (size_t start = 0; start < soldiers.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, soldiers.count);
        // rng captured BY VALUE (8 bytes): the job outlives this stack frame.
        jobSystem.submit([this, start, end, dt, rng]() {
            phaseSoldierSteerChunk(start, end, dt, rng);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }

    // Barrier owned by tick() (Design Doc §6.3), not this function -- see
    // the comment on tick()'s own jobSystem.waitAll() calls.
}

void Simulation::phaseSoldierSteerChunk(size_t start, size_t end, float dt, Rng rng) {
    // Collision avoidance using spatial queries (Phase 2). Radius is
    // kSeparationRadius (Formation.hpp): it must stay below kSlotSpacing so
    // separation only prevents overlap and never fights a held formation --
    // see that constant's comment for the derivation.
    const float separationRadius = kSeparationRadius;
    const float separationStrength = 300.0f;  // Increased from 200
    const float separationRadiusSq = separationRadius * separationRadius;

    // Thread-local neighbor buffer
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);

    for (size_t i = start; i < end; i++) {
        // Sets base velocity toward this soldier's formation slot. Must run
        // first: separation and obstacle avoidance below ADD to velocity,
        // so calling this after would erase them instead of blending in.
        steerToSlot(soldiers, squads, i, dt);

        float px = soldiers.posX[i];
        float py = soldiers.posY[i];

        // Query nearby neighbors (Design Doc §5.4)
        spatialHash.queryNeighbors(px, py, separationRadius, localNeighbors);

        float steerX = 0.0f;
        float steerY = 0.0f;

        // Calculate separation force from neighbors
        for (uint32_t neighborIdx : localNeighbors) {
            if (neighborIdx == i) continue;  // Skip self

            float dx = px - soldiers.posX[neighborIdx];
            float dy = py - soldiers.posY[neighborIdx];
            float distSq = dx * dx + dy * dy;

            if (distSq < separationRadiusSq && distSq > 0.01f) {
                float dist = std::sqrt(distSq);
                // Stronger force when closer
                float force = (separationRadius - dist) / separationRadius;
                steerX += (dx / dist) * force;
                steerY += (dy / dist) * force;
            }
        }

        // Obstacle avoidance - buildings (rectangles)
        for (size_t b = 0; b < buildings.size(); b++) {
            const auto& building = buildings[b];
            // Find closest point on rectangle to agent
            float closestX = std::max(building.x, std::min(px, building.x + building.width));
            float closestY = std::max(building.y, std::min(py, building.y + building.height));

            float dx = px - closestX;
            float dy = py - closestY;
            float distSq = dx * dx + dy * dy;

            const float obstacleAvoidDist = 50.0f;  // Start avoiding earlier
            if (distSq < obstacleAvoidDist * obstacleAvoidDist) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly in any direction.
                    // Buildings are generated without overlap rejection, so one agent
                    // can be inside two at once and reach this line twice per tick.
                    // The obstacle index is folded into the key: without it both draws
                    // share an input, always return the same sign, and can only
                    // reinforce -- the old code's cancelling case became unreachable.
                    const uint32_t key = (uint32_t)(i * buildings.size() + b);
                    steerX += (rng.range(key, RngUse::SeparationPushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationPushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (obstacleAvoidDist - dist) / obstacleAvoidDist;
                    steerX += (dx / dist) * force * 5.0f;  // Much stronger avoidance
                    steerY += (dy / dist) * force * 5.0f;
                }
            }
        }

        // Obstacle avoidance - trees (circles)
        for (size_t t = 0; t < trees.size(); t++) {
            const auto& tree = trees[t];
            float dx = px - tree.x;
            float dy = py - tree.y;
            float distSq = dx * dx + dy * dy;
            float avoidRadius = tree.radius + 20.0f;  // Extra buffer

            if (distSq < avoidRadius * avoidRadius) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly. Same per-obstacle keying as
                    // the building push above; two tree centres within 0.1px of each
                    // other is practically unreachable, but the shape should match.
                    const uint32_t key = (uint32_t)(i * trees.size() + t);
                    steerX += (rng.range(key, RngUse::SeparationTreePushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationTreePushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (avoidRadius - dist) / avoidRadius;
                    steerX += (dx / dist) * force * 5.0f;
                    steerY += (dy / dist) * force * 5.0f;
                }
            }
        }

        // Apply separation steering
        soldiers.velX[i] += steerX * separationStrength * dt;
        soldiers.velY[i] += steerY * separationStrength * dt;

        // Limit velocity
        const float maxSpeed = 150.0f;
        float speedSq = soldiers.velX[i] * soldiers.velX[i] + soldiers.velY[i] * soldiers.velY[i];
        if (speedSq > maxSpeed * maxSpeed) {
            float speed = std::sqrt(speedSq);
            soldiers.velX[i] = (soldiers.velX[i] / speed) * maxSpeed;
            soldiers.velY[i] = (soldiers.velY[i] / speed) * maxSpeed;
        }
    }
}

void Simulation::phaseMovement(float dt) {
    // Parallelize movement integration (Design Doc §6.2)
    const size_t chunkSize = 256;

    for (size_t start = 0; start < soldiers.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, soldiers.count);
        jobSystem.submit([this, start, end, dt]() {
            phaseMovementChunk(start, end, dt);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }

    // Barrier owned by tick(), not this function -- see the comment on
    // tick()'s own jobSystem.waitAll() calls.
}

void Simulation::phaseMovementChunk(size_t start, size_t end, float dt) {
    // SIMD-friendly: compiler auto-vectorizes this loop
    for (size_t i = start; i < end; i++) {
        float newX = soldiers.posX[i] + soldiers.velX[i] * dt;
        float newY = soldiers.posY[i] + soldiers.velY[i] * dt;

        // Check collision with buildings
        bool blocked = false;
        for (const auto& building : buildings) {
            if (newX > building.x - 5 && newX < building.x + building.width + 5 &&
                newY > building.y - 5 && newY < building.y + building.height + 5) {
                // Inside or very close to building - block movement
                blocked = true;
                // Find which side we hit
                float centerX = building.x + building.width / 2.0f;
                float centerY = building.y + building.height / 2.0f;
                float dx = soldiers.posX[i] - centerX;
                float dy = soldiers.posY[i] - centerY;

                // Push out and deflect velocity
                if (std::abs(dx) > std::abs(dy)) {
                    // Hit horizontal side - deflect horizontally, keep Y velocity
                    newX = soldiers.posX[i] + (dx > 0 ? 2.0f : -2.0f);
                    soldiers.velX[i] = -soldiers.velX[i] * 0.3f;  // Bounce back weakly
                    // Keep Y velocity to slide along wall
                } else {
                    // Hit vertical side - deflect vertically, keep X velocity
                    newY = soldiers.posY[i] + (dy > 0 ? 2.0f : -2.0f);
                    soldiers.velY[i] = -soldiers.velY[i] * 0.3f;  // Bounce back weakly
                    // Keep X velocity to slide along wall
                }
                break;
            }
        }

        // Check collision with trees
        if (!blocked) {
            for (const auto& tree : trees) {
                float dx = newX - tree.x;
                float dy = newY - tree.y;
                float distSq = dx * dx + dy * dy;
                if (distSq < tree.radius * tree.radius) {
                    // Inside tree - block and push out
                    blocked = true;
                    float dist = std::sqrt(distSq + 0.01f);
                    newX = tree.x + (dx / dist) * (tree.radius + 2.0f);
                    newY = tree.y + (dy / dist) * (tree.radius + 2.0f);
                    // Deflect velocity tangentially (slide around)
                    float normalX = dx / dist;
                    float normalY = dy / dist;
                    float velDotNormal = soldiers.velX[i] * normalX + soldiers.velY[i] * normalY;
                    soldiers.velX[i] -= normalX * velDotNormal * 1.5f;  // Remove normal component
                    soldiers.velY[i] -= normalY * velDotNormal * 1.5f;
                    break;
                }
            }
        }

        soldiers.posX[i] = newX;
        soldiers.posY[i] = newY;

        // Update direction from velocity (for rendering)
        float speed = std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                               soldiers.velY[i] * soldiers.velY[i]);
        if (speed > 0.1f) {  // Only update if moving
            soldiers.dirX[i] = soldiers.velX[i] / speed;
            soldiers.dirY[i] = soldiers.velY[i] / speed;
        }
    }
}

void Simulation::clampToWorld() {
    const float w = static_cast<float>(worldWidth);
    const float h = static_cast<float>(worldHeight);
    const float damping = 0.5f; // Bounce damping factor

    for (size_t i = 0; i < soldiers.count; i++) {
        // Left boundary
        if (soldiers.posX[i] < 0) {
            soldiers.posX[i] = 0;
            soldiers.velX[i] = std::abs(soldiers.velX[i]) * damping; // Bounce right
        }
        // Right boundary
        if (soldiers.posX[i] > w) {
            soldiers.posX[i] = w;
            soldiers.velX[i] = -std::abs(soldiers.velX[i]) * damping; // Bounce left
        }
        // Top boundary
        if (soldiers.posY[i] < 0) {
            soldiers.posY[i] = 0;
            soldiers.velY[i] = std::abs(soldiers.velY[i]) * damping; // Bounce down
        }
        // Bottom boundary
        if (soldiers.posY[i] > h) {
            soldiers.posY[i] = h;
            soldiers.velY[i] = -std::abs(soldiers.velY[i]) * damping; // Bounce up
        }
    }
}

uint32_t Simulation::getMaxCellOccupancy() const {
    return spatialHash.getMaxOccupancy();
}

uint64_t Simulation::stateDigest() const {
    StateDigest d;
    d.mix(static_cast<uint32_t>(soldiers.count));
    for (size_t i = 0; i < soldiers.count; ++i) {
        d.mix(soldiers.posX[i]);
        d.mix(soldiers.posY[i]);
        d.mix(soldiers.velX[i]);
        d.mix(soldiers.velY[i]);
        d.mix(static_cast<uint32_t>(soldiers.team[i]));
        d.mix(static_cast<uint32_t>(soldiers.unitType[i]));
        d.mix(static_cast<uint32_t>(soldiers.state[i]));
        d.mix(static_cast<uint32_t>(soldiers.squadId[i]));
        d.mix(static_cast<uint32_t>(soldiers.slotIndex[i]));
        d.mix(static_cast<uint32_t>(soldiers.health[i]));
        // intentTarget/intentFire/attackCooldown are constant today (plan 2
        // makes them live inside phaseResolution, the phase with the most
        // cross-agent pressure); dirX/dirY are set at deployment and by the
        // speed-gated update in phaseMovementChunk. All five are included
        // now, ahead of plan 2, so the thread-invariance gate already
        // exercises them the day they start changing instead of being
        // blind to a divergence introduced then.
        d.mix(soldiers.intentTarget[i]);
        d.mix(static_cast<uint32_t>(soldiers.intentFire[i]));
        d.mix(soldiers.attackCooldown[i]);
        d.mix(soldiers.dirX[i]);
        d.mix(soldiers.dirY[i]);
    }

    // The squad tier now has real per-tick state (centroid, facing) written
    // by a parallel-across-squads phase. A digest that only covered soldiers
    // would pass the thread-invariance gate even if that phase diverged
    // across thread counts, since none of its output would ever be
    // compared. Digest it so the gate actually exercises what this task
    // added. order/targetSquad/morale/discipline are constant today (plan 3
    // gives them meaning) but are included now so the gate already covers
    // them once that lands.
    d.mix(static_cast<uint32_t>(squads.count));
    for (size_t s = 0; s < squads.count; ++s) {
        d.mix(static_cast<uint32_t>(squads.memberCount[s]));
        d.mix(static_cast<uint32_t>(squads.order[s]));
        d.mix(static_cast<uint32_t>(squads.targetSquad[s]));
        d.mix(squads.centroidX[s]);
        d.mix(squads.centroidY[s]);
        d.mix(squads.facingX[s]);
        d.mix(squads.facingY[s]);
        d.mix(squads.morale[s]);
        d.mix(squads.discipline[s]);
    }
    return d.value();
}
