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
    std::vector<uint32_t> targetSoldier;  // plan 3
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
