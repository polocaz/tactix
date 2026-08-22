#include "platform.h"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Soldiers.hpp"
#include "Formation.hpp"
#include "StateDigest.hpp"
#include "DetMath.hpp"
#include "Combat.hpp"
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

// Composition scales with squad count rather than cycling a fixed modulo,
// so a small army still fields all three types. The old `sq % 20` (archers
// at bucket 12..16, cavalry at 17..19) needed 13 squads per team before the
// first archer appeared -- about 601 agents -- which made every army below
// that pure infantry, invisible to the GUI's agent slider and to the
// projectile system tasks 7-9 build. Regression test at 500 agents (the
// measured failure point) lives in test_combat.cpp.
//
// Below 2 squads per team the 60/25/15 split has no room: a lone squad
// would land at or past cavalryStart (0) and come out all-cavalry, a worse
// degenerate case than what this replaces. Floored at Infantry instead,
// matching what a single squad already was under the old formula, so the
// smallest possible army gets a sensible defender rather than an arbitrary
// lone horseman.
//
// At exactly 2 squads per team -- 100 agents, the GUI's default AND its
// slider minimum -- a naive 60/85 percent split puts archerStart and
// cavalryStart on the same bucket (both round to 1), so squad 1 comes out
// Cavalry and the army fields zero archers. That is precisely the case this
// composition exists to fix: a user watching the default battle with the
// slider untouched should see archers, not just infantry and cavalry.
// Special-cased to Infantry + Archer (no cavalry) rather than the reverse,
// since Infantry is the one type that must always be present (see above)
// and Archer is the type the fix is FOR.
//
// At 3+ squads per team, archerStart/cavalryStart are clamped so each type
// always claims at least one squad, instead of trusting the 60/85 percent
// split (which can still collide at small squadsPerTeam) to land them apart.
static UnitType unitTypeForSquad(uint32_t sq, uint32_t squadsPerTeam) {
    if (squadsPerTeam < 2) return UnitType::Infantry;
    if (squadsPerTeam == 2) return (sq == 0) ? UnitType::Infantry : UnitType::Archer;

    // Integer arithmetic on purpose: unit type feeds the state digest, and
    // no float belongs in a decision that does.
    uint32_t archerStart  = (squadsPerTeam * 60) / 100;
    if (archerStart < 1) archerStart = 1;
    uint32_t cavalryStart = (squadsPerTeam * 85) / 100;
    if (cavalryStart <= archerStart) cavalryStart = archerStart + 1;
    if (cavalryStart >= squadsPerTeam) cavalryStart = squadsPerTeam - 1;
    if (sq >= cavalryStart) return UnitType::Cavalry;
    if (sq >= archerStart)  return UnitType::Archer;
    return UnitType::Infantry;
}

void Simulation::init(size_t soldierCount) {
    spdlog::info("Initializing {} agents", soldierCount);
    // init() runs before any tick, so tick 0 is reserved for setup draws.
    const Rng rng{worldSeed, 0u};
    soldiers.reserve(soldierCount);
    prevPosX.reserve(soldierCount);
    prevPosY.reserve(soldierCount);

    generateObstacles();

    // Squad composition by count: approximately 60% infantry, 25% archers,
    // 15% cavalry -- "approximately" because unitTypeForSquad works in
    // integer squad-count buckets, not soldier counts, so e.g. 500 agents
    // (10 squads/team) lands on 60/20/20 (6 infantry, 2 archer, 2 cavalry
    // squads), not an exact 60/25/15.
    constexpr uint32_t kSquadSize = 25;
    // Team A gets the floor half, team B the remainder, so an odd
    // soldierCount still deploys every soldier requested (off by one
    // between the two armies rather than one soldier short overall).
    const size_t perTeamA = soldierCount / 2;
    const size_t perTeamB = soldierCount - perTeamA;

    // Composition (which unit type squad index N gets) is driven by ONE
    // shared squadsPerTeam for both teams, not each team's own. perTeamA and
    // perTeamB can differ by one soldier when soldierCount is odd, and
    // because squadsPerTeam is a ceiling division by kSquadSize, that single
    // soldier can tip one team over a kSquadSize boundary while the other
    // stays under it (e.g. 50 vs 51 soldiers -> 2 vs 3 squads/team). Feeding
    // each team its OWN squadsPerTeam let squad index N mean a different
    // unit type depending on which team it belonged to -- the two armies
    // fielded different rosters, which breaks the spec's opening requirement
    // of two symmetric armies. Using the larger of the two here means squad
    // index N always means the same type on both sides; the smaller team
    // simply never reaches as high an index.
    const uint32_t squadsPerTeamA = (uint32_t)((perTeamA + kSquadSize - 1) / kSquadSize);
    const uint32_t squadsPerTeamB = (uint32_t)((perTeamB + kSquadSize - 1) / kSquadSize);
    const uint32_t compositionSquadsPerTeam = std::max(squadsPerTeamA, squadsPerTeamB);

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
        const size_t perTeam = (t == 0) ? perTeamA : perTeamB;
        const uint32_t squadsPerTeam = (t == 0) ? squadsPerTeamA : squadsPerTeamB;
        if (squadsPerTeam == 0) continue;

        // Team A faces right from the left margin, team B faces left.
        const float baseX = (t == 0) ? w * 0.15f : w * 0.85f;
        const float facing = (t == 0) ? 1.0f : -1.0f;
        // Depth behind the front line before the corridor runs into the
        // world edge -- exactly the distance from baseX to that edge, so a
        // column offset (below) can never carry a squad past it.
        const float colBandDepth = (t == 0) ? baseX : (w - baseX);

        // Pass 1: every squad's shape and member count is knowable without
        // touching a soldier, so scan them first for the largest formation
        // footprint (Formation.hpp's formationExtent) this team will
        // actually deploy. That sets the spacing floor neighbouring squads
        // need so they do not overlap (finding 2). Floored at kSlotSpacing
        // so a pitch is never zero regardless of member counts.
        float neededRowPitch = kSlotSpacing;
        float neededColPitch = kSlotSpacing;
        for (uint32_t sq = 0; sq < squadsPerTeam; ++sq) {
            UnitType unit = unitTypeForSquad(sq, compositionSquadsPerTeam);
            const uint32_t members = (uint32_t)std::min<size_t>(
                kSquadSize, perTeam - (size_t)sq * kSquadSize);
            const Vec2 extent = formationExtent(shapeForUnit(unit), members);
            neededRowPitch = std::max(neededRowPitch, extent.x);
            neededColPitch = std::max(neededColPitch, extent.y);
        }

        // Vertical band squads stack down, margined in from both edges by
        // at least the widest squad's own footprint (neededRowPitch), not
        // a fixed h*0.1. A fixed margin was fine while every squad was
        // Infantry-width, but unitTypeForSquad can now put a wide Loose
        // archer squad on the outermost row, and its width alone
        // (kSlotSpacing * 2 wider than Line) can exceed a fixed 0.1h
        // margin on a small field -- the squad origin would sit outside
        // the world, soldiers would be clamped onto the edge below, and
        // slotError would blow past its jitter bound.
        //
        // The full footprint, not half of it, because formationExtent's
        // bounding box is not guaranteed centered on the centroid --
        // formationMeanOffset only guarantees the centroid is the MEAN of
        // member offsets, and a formation's last, partially-filled rank
        // can pull that mean off-center. The centroid is always inside its
        // own bounding box, though, so its distance to either edge can
        // never exceed the box's full width -- that bound holds regardless
        // of how asymmetric the box is. Flooring at h*0.1 keeps the old
        // margin for the common case where it was already enough.
        const float rowMargin = std::max(h * 0.1f, neededRowPitch);
        // Floored at neededRowPitch, not just >0: a squad wider than the
        // field itself would otherwise drive this negative, and casting a
        // negative float to the uint32_t perColumn division below is
        // undefined behaviour. The existing packedTighter path already
        // handles "narrower than needed" (rowPitch < neededRowPitch)
        // gracefully, so flooring here just routes this extreme case
        // through that same, already-correct degrade instead of a new one.
        const float rowBand = std::max(neededRowPitch, h - 2.0f * rowMargin);

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
            UnitType unit = unitTypeForSquad(sq, compositionSquadsPerTeam);

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
            const float squadY = rowMargin + (float)row * rowPitch;

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
                // Spawn exactly on the slot steerToSlot will target, using the
                // same rotation (slotWorldPosition), so tick 1 moves nobody.
                // Squad centroid/facing above must be set before this call.
                //
                // Clear AFTER jitter (design §5.1): the raw slot is already
                // obstacle-cleared, but a soldier standing at the standoff
                // boundary can be jittered back inside a wall before spawning.
                // Clearing the jittered point is the correctness fix; jitter
                // stays transient deployment noise and is NOT baked into the
                // persistent slot target (§5.2), so steering still settles to
                // the true assigned slot. This intentionally moves the state
                // digest for soldiers whose jitter crossed the standoff.
                const Vec2 rawSlot = slotWorldPosition(squads, squadId, (uint16_t)k,
                                                       squads.memberCount[squadId]);
                const Vec2 jittered{ rawSlot.x + jx, rawSlot.y + jy };
                const Vec2 clear = clearOfObstacles(jittered);

                soldiers.spawn(clampf(clear.x, 0.0f, w), clampf(clear.y, 0.0f, h),
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
    rebuildSquadMembers(soldiers, squads, squadMembers, squadMemberCounts, squadMemberCursor);

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
    // Measured against the same obstacle-cleared point steering actually
    // sends the soldier to, not the raw grid slot: a slot inside a wall is
    // not an error the soldier can close.
    const Vec2 t = clearOfObstacles(slotWorldPosition(squads, s, soldiers.slotIndex[i],
                                                      squads.memberCount[s]));
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
    squads.objectiveX.clear();
    squads.objectiveY.clear();
    squads.moveX.clear();
    squads.moveY.clear();
    squads.count = 0;
    squadMembers.clear();
    squadMemberCounts.clear();
    squadMemberCursor.clear();
    casualties.clear();
    officerDied.clear();

    projectiles.clear();

    prevPosX.clear();
    prevPosY.clear();

    terrain.buildings.clear();
    terrain.trees.clear();

    tickNumber = 0;

    init(count);
}

void Simulation::generateObstacles() {
    // Runs before any tick; no agent involved, so the obstacle index keys the draw.
    const Rng rng{worldSeed, 0u};

    // Obstacles are placed with rejection sampling so that no two of them come
    // within kObstacleStandoff of each other. That gap is not decoration: it is
    // what makes clearOfObstacles' push correct. Sliding a point kObstacleStandoff
    // clear of one obstacle only helps if it cannot land inside the next one,
    // and with the old overlap-free-for-all placement it regularly did -- 263
    // of 18k sampled field positions stayed embedded in a wall no matter how
    // many push passes ran, because two overlapping buildings bounced the
    // point back and forth between them. Keeping the obstacles apart removes
    // the trap instead of teaching the push to escape it.
    //
    // A placement that cannot find a free spot in kPlacementTries is dropped
    // rather than forced, so a crowded field simply gets fewer trees. Each
    // try folds the attempt number into the draw key, the same way every
    // other repeated draw in this file varies its input.
    constexpr uint32_t kPlacementTries = 24;
    const float gap = kObstacleStandoff;

    auto farEnough = [&](float minX, float minY, float maxX, float maxY) {
        for (const auto& b : terrain.buildings) {
            if (minX < b.x + b.width + gap && maxX > b.x - gap &&
                minY < b.y + b.height + gap && maxY > b.y - gap) {
                return false;
            }
        }
        for (const auto& t : terrain.trees) {
            if (minX < t.x + t.radius + gap && maxX > t.x - t.radius - gap &&
                minY < t.y + t.radius + gap && maxY > t.y - t.radius - gap) {
                return false;
            }
        }
        return true;
    };

    // City blocks (buildings)
    const int blockCount = 8;
    for (int i = 0; i < blockCount; i++) {
        for (uint32_t attempt = 0; attempt < kPlacementTries; ++attempt) {
            const uint32_t key = (uint32_t)i * kPlacementTries + attempt;
            float x = (float)rng.range(key, RngUse::ObstacleBuildingX, 100, worldWidth - 200);
            float y = (float)rng.range(key, RngUse::ObstacleBuildingY, 100, worldHeight - 200);
            float w = (float)rng.range(key, RngUse::ObstacleBuildingW, 80, 150);
            float h = (float)rng.range(key, RngUse::ObstacleBuildingH, 80, 150);
            if (!farEnough(x, y, x + w, y + h)) continue;
            terrain.buildings.push_back({x, y, w, h});
            break;
        }
    }

    // Scattered trees
    const int treeCount = 30;
    for (int i = 0; i < treeCount; i++) {
        for (uint32_t attempt = 0; attempt < kPlacementTries; ++attempt) {
            const uint32_t key = (uint32_t)i * kPlacementTries + attempt;
            float x = (float)rng.range(key, RngUse::ObstacleTreeX, 50, worldWidth - 50);
            float y = (float)rng.range(key, RngUse::ObstacleTreeY, 50, worldHeight - 50);
            float r = (float)rng.range(key, RngUse::ObstacleTreeRadius, 15, 25);
            if (!farEnough(x - r, y - r, x + r, y + r)) continue;
            terrain.trees.push_back({x, y, r});
            break;
        }
    }

    spdlog::info("Generated {} buildings and {} trees", terrain.buildings.size(), terrain.trees.size());
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
                selectTargetSoldier(soldiers, squads, squadMembers, s);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}

void Simulation::phaseSquadDecide(const Rng&) {
    // Plan 3 replaces the body of selectTargetSquad with a weighted scorer over
    // seven orders, plus hysteresis and a decide stagger. The dispatch shape
    // here does not change.
    const size_t chunkSize = 32;
    for (size_t start = 0; start < squads.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, squads.count);
        jobSystem.submit([this, start, end]() {
            for (size_t s = start; s < end; ++s) {
                selectTargetSquad(squads, s, terrain);
                workCounters.add(workCounters.squadDecisions, 1);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}

void Simulation::phaseProjectiles(float dt) {
    // Parallel over projectiles. Each job writes only its own projectile's
    // fields (posX/posY/lifetime/intentHitTarget) and reads soldiers, which
    // is safe here because soldier positions are written only in phase 7
    // (phase 4 writes velocity and intents, not position), not phase 5.
    const size_t chunkSize = 128;
    for (size_t start = 0; start < projectiles.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, projectiles.count);
        jobSystem.submit([this, start, end, dt]() {
            std::vector<uint32_t> scratch;
            scratch.reserve(64);
            for (size_t i = start; i < end; ++i) {
                integrateProjectile(projectiles, soldiers, spatialHash, i, dt, scratch);
                workCounters.add(workCounters.projectileHitTests, 1);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}

void Simulation::phaseResolution(const Rng& rng) {
    // Spec 5.5. The order is load-bearing and each step notes what it needs.
    // Single-threaded on purpose: this is the ONLY place cross-agent mutation
    // is permitted anywhere in the tick.
    casualties.assign(squads.count, 0u);
    officerDied.assign(squads.count, 0u);

    applyMeleeIntents(soldiers);                            // step 1
    applyProjectileHits(projectiles, soldiers, rng);        // step 2
    spawnArrows(soldiers, squads, projectiles, rng);         // step 3
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.intentFire[i]) {
            soldiers.attackCooldown[i] = kArcherCooldown;
            soldiers.intentFire[i] = 0;
        }
    }
    recordCasualties(soldiers, casualties, officerDied);    // step 4
    // step 5 (morale and discipline) is plan 3; casualties and officerDied are
    // recorded now precisely so it has something to read when it arrives.
    compactProjectiles(projectiles);
    compactDead(soldiers, prevPosX, prevPosY);              // step 6
    rebuildSquadMembers(soldiers, squads, squadMembers,     // step 7
                       squadMemberCounts, squadMemberCursor);

    // targetSoldier is only meaningful between phase 2 (where it is
    // computed) and step 3 above (where spawnArrows consumes it).
    // compactDead just renumbered soldiers, so any index still sitting in
    // targetSoldier now is stale: it names a soldier under the OLD
    // numbering, not whoever occupies that slot after compaction. Left in
    // place, that stale index would be read by squadTargetSoldier() and by
    // stateDigest() below as if it still meant something -- a same-team or
    // simply wrong soldier, deterministically but meaninglessly. Clearing
    // it here means nothing outside this tick ever observes a stale value;
    // phase 2 recomputes it from scratch every tick anyway, so this costs
    // nothing.
    for (size_t s = 0; s < squads.count; ++s) {
        squads.targetSoldier[s] = UINT32_MAX;
    }
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
        // The slot goes through clearOfObstacles rather than being used raw:
        // a slot inside a building is a target no soldier can ever reach, and
        // one sent there grinds against the wall for the whole battle.
        {
            const uint16_t sq = soldiers.squadId[i];
            steerToward(soldiers, i,
                        clearOfObstacles(slotWorldPosition(squads, sq, soldiers.slotIndex[i],
                                                           squads.memberCount[sq])),
                        dt);
        }

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
        for (size_t b = 0; b < terrain.buildings.size(); b++) {
            const auto& building = terrain.buildings[b];
            // Find closest point on rectangle to agent
            float closestX = std::max(building.x, std::min(px, building.x + building.width));
            float closestY = std::max(building.y, std::min(py, building.y + building.height));

            float dx = px - closestX;
            float dy = py - closestY;
            float distSq = dx * dx + dy * dy;

            // Sized like separation, not like a keep-out zone. At the old
            // 50px/x5 the push was ~750px/s against an infantryman who walks
            // at 45: every building carried a 50px halo no formation could
            // stand in, and any squad ordered near one was shoved off its
            // slots. Hard collision in phaseMovementChunk is what stops
            // soldiers entering a building; this only has to make them
            // round the corner rather than walk into it.
            const float obstacleAvoidDist = kObstacleStandoff;
            if (distSq < obstacleAvoidDist * obstacleAvoidDist) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly in any direction.
                    // generateObstacles now keeps obstacles kObstacleStandoff
                    // apart, so an agent can no longer be inside two at once
                    // and this should fire at most once per agent-tick. The
                    // obstacle index stays folded into the key anyway: it costs
                    // nothing, and without it two draws in one tick would share
                    // an input, always return the same sign, and could only
                    // reinforce rather than cancel.
                    const uint32_t key = (uint32_t)(i * terrain.buildings.size() + b);
                    steerX += (rng.range(key, RngUse::SeparationPushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationPushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (obstacleAvoidDist - dist) / obstacleAvoidDist;
                    steerX += (dx / dist) * force;
                    steerY += (dy / dist) * force;
                }
            }
        }

        // Obstacle avoidance - trees (circles)
        for (size_t t = 0; t < terrain.trees.size(); t++) {
            const auto& tree = terrain.trees[t];
            float dx = px - tree.x;
            float dy = py - tree.y;
            float distSq = dx * dx + dy * dy;
            // Same scaling argument as the building standoff above: a soldier
            // brushes past a trunk, it does not orbit it at 20px.
            const float avoidRadius = tree.radius + kObstacleStandoff;

            if (distSq < avoidRadius * avoidRadius) {
                if (distSq < 0.01f) {
                    // Inside obstacle - push out strongly. Same per-obstacle keying as
                    // the building push above; two tree centres within 0.1px of each
                    // other is practically unreachable, but the shape should match.
                    const uint32_t key = (uint32_t)(i * terrain.trees.size() + t);
                    steerX += (rng.range(key, RngUse::SeparationTreePushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                    steerY += (rng.range(key, RngUse::SeparationTreePushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
                } else {
                    float dist = std::sqrt(distSq);
                    float force = (avoidRadius - dist) / avoidRadius;
                    steerX += (dx / dist) * force;
                    steerY += (dy / dist) * force;
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

        // Melee target selection (Task 3). Writes only soldiers.intentTarget[i]
        // and reuses localNeighbors, the buffer separation just filled above.
        selectMeleeTarget(soldiers, spatialHash, i, localNeighbors);

        // Archers fire at whatever their squad handed them, subject to cooldown.
        // Writing only our own flag keeps this parallel-safe; resolution turns
        // flags into arrows.
        soldiers.intentFire[i] = 0;
        if (soldiers.unitType[i] == UnitType::Archer &&
            soldiers.attackCooldown[i] <= 0.0f &&
            soldiers.state[i] != SoldierState::Dead) {
            const uint16_t sq = soldiers.squadId[i];
            if (sq < squads.count && squads.targetSoldier[sq] != UINT32_MAX) {
                soldiers.intentFire[i] = 1;
            }
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
        for (const auto& building : terrain.buildings) {
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
            for (const auto& tree : terrain.trees) {
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

        // Decay the melee attack cooldown (Task 3). This phase already
        // writes only its own soldier, so it is safe to do here too.
        if (soldiers.attackCooldown[i] > 0.0f) {
            soldiers.attackCooldown[i] -= dt;
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
        d.mix(squads.targetSoldier[s]);
        d.mix(squads.centroidX[s]);
        d.mix(squads.centroidY[s]);
        d.mix(squads.facingX[s]);
        d.mix(squads.facingY[s]);
        d.mix(squads.morale[s]);
        d.mix(squads.discipline[s]);
        // Terrain tactical objective / movement direction (design §7). New squad
        // fields, so they belong in the digest: a divergence here across worker
        // counts would otherwise be invisible to the thread-invariance gate.
        d.mix(squads.objectiveX[s]);
        d.mix(squads.objectiveY[s]);
        d.mix(squads.moveX[s]);
        d.mix(squads.moveY[s]);
    }

    // Projectiles are included from the moment the array exists, so the
    // thread-invariance gate covers them before anything starts writing them.
    d.mix(static_cast<uint32_t>(projectiles.count));
    for (size_t i = 0; i < projectiles.count; ++i) {
        d.mix(projectiles.posX[i]);
        d.mix(projectiles.posY[i]);
        d.mix(projectiles.velX[i]);
        d.mix(projectiles.velY[i]);
        d.mix(static_cast<uint32_t>(projectiles.team[i]));
        d.mix(projectiles.lifetime[i]);
    }
    return d.value();
}
