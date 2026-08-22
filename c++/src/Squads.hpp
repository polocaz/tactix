#pragma once
#include "Units.hpp"
#include "Terrain.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;  // defined in Simulation.hpp

struct SquadHot {
    std::vector<Team>     team;
    std::vector<UnitType> unitType;
    std::vector<float>    centroidX, centroidY;
    std::vector<float>    facingX, facingY;
    std::vector<uint8_t>  order;          // plan 3 gives this meaning
    std::vector<uint16_t> targetSquad;
    // Written every tick by selectTargetSoldier (Simulation::phaseSquadAggregate,
    // phase 2, parallel across squads), from the previous tick's targetSquad.
    // UINT32_MAX when the squad has no ranged weapon or no target in range.
    std::vector<uint32_t> targetSoldier;
    std::vector<float>    morale;         // plan 3
    std::vector<float>    discipline;     // plan 3
    std::vector<uint32_t> memberStart, memberCount;

    // Terrain-aware tactical objective (design §7.1): the point this squad's
    // centroid should aim for, chosen from candidate anchors around obstacles.
    // Separate from targetSquad ("who we care about") and from facingX/Y
    // ("where we point", usually the enemy centroid). objectiveX/Y is the
    // WHERE TERRAIN SAYS WE STAND; moveX/Y is its normalized direction, used
    // for the advance lead so a squad can side-step into clear ground while
    // still facing the enemy (§7.2). Both written only by this squad in phase
    // 3, so they preserve the thread-count-invariance rule.
    std::vector<float>    objectiveX, objectiveY;
    std::vector<float>    moveX, moveY;

    size_t count = 0;

    void spawn(Team t, UnitType u) {
        team.push_back(t);
        unitType.push_back(u);
        centroidX.push_back(0.0f);
        centroidY.push_back(0.0f);
        facingX.push_back(1.0f);
        facingY.push_back(0.0f);
        order.push_back(0);
        targetSquad.push_back(0);
        targetSoldier.push_back(UINT32_MAX);
        morale.push_back(1.0f);
        discipline.push_back(1.0f);
        memberStart.push_back(0);
        memberCount.push_back(0);
        objectiveX.push_back(0.0f);
        objectiveY.push_back(0.0f);
        moveX.push_back(1.0f);
        moveY.push_back(0.0f);
        count++;
    }
};

// Regroups `members` by squad, ordering each squad's range by the soldiers'
// previous slotIndex, then reassigns slotIndex densely from zero.
//
// Ordering by previous slotIndex rather than array position is a correctness
// requirement: it is what makes a dead officer's successor the soldier who
// was standing next to them, instead of an arbitrary survivor whose position
// would teleport the formation's anchor.
//
// countsScratch/cursorScratch are caller-owned scratch buffers, not output:
// this runs once a tick in serial resolution, and owning them lets the
// caller keep their capacity across ticks instead of paying two heap
// allocations every tick.
void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members,
                         std::vector<uint32_t>& countsScratch,
                         std::vector<uint32_t>& cursorScratch);

// Recomputes each squad's centroid from its members. Parallel-safe: writes
// only the squad it is given, reads only that squad's members.
void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t squadIndex);

// Picks the nearest enemy squad by centroid distance. Parallel-safe: writes
// only the squad it is given, reads other squads' centroids, which phase 2
// already finished writing.
//
// Plan 3 replaces this with the weighted scorer. The FIELD it writes stays the
// same, so only the choice changes, not the plumbing.
void selectTargetSquad(SquadHot& squads, size_t squadIndex, const TerrainField& terrain,
                       float dt);

// Rotates `current` toward `desired` by at most `maxRadians`, returning a unit
// vector. Falls back to `current` (normalized) when `desired` is degenerate,
// and to (1,0) when both are, so this never produces NaN.
//
// Deliberately avoids atan2: there is no deterministic atan2 in DetMath, and
// none is needed. The dot product answers "are we within one step" and the
// cross product answers "which way", which is the whole decision.
Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians);

// Chooses a terrain-aware tactical objective for squad `s` (design §6/§7) and
// writes it to outObjective/outMove. Reads only squad `s` and its target's
// centroids plus `terrain`; writes nothing else, so it is parallel-safe inside
// phase 3. Deterministic: candidate order is fixed and the scorer breaks ties
// by candidate index (design §7.3). Direct lane clear -> direct objective; a
// blocked lane -> a clear anchor around the obstacle (§7.4).
void chooseTacticalObjective(const TerrainField& terrain, const SquadHot& squads,
                             size_t squadIndex, Vec2& outObjective, Vec2& outMove);

// Spec 6.5. Picks the member of targetSquad with the LOWEST slotIndex that is
// within weapon range of our centroid, or UINT32_MAX if none is.
//
// Recomputed every tick and never cached across ticks: compaction renumbers
// soldiers, so a stored soldier index is stale the moment anyone dies.
//
// Lowest slotIndex is what makes officers preferentially targeted without a
// special case, since the officer is whoever holds slot 0.
void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t squadIndex);

// Normalizes a squad's facing vector in place, falling back to (1, 0) for a
// zero-length input rather than producing NaN. Shared by deployment
// (Simulation::init, which must establish the unit-length invariant slot
// rotation depends on) and updateSquadAggregate (which maintains it every
// tick), so both routes go through one definition of "unit length."
void normalizeFacing(SquadHot& squads, size_t squadIndex);
