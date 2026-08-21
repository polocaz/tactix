#pragma once
#include "Units.hpp"
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
void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members);

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
void selectTargetSquad(SquadHot& squads, size_t squadIndex);

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
