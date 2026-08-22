#include "Contact.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "SpatialHash.hpp"
#include "Formation.hpp"
#include <cmath>

void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t s, float dt,
                   std::vector<uint32_t>& scratch) {
    const uint32_t start = squads.memberStart[s];
    const uint32_t n     = squads.memberCount[s];

    if (n == 0) {
        squads.contact[s] = 0;
        squads.contactTimer[s] = 0.0f;
        return;
    }

    const FormationShape shape = shapeForUnit(squads.unitType[s]);
    const Team ownTeam = squads.team[s];
    const float radiusSq = kContactRadius * kContactRadius;

    uint32_t frontRankCount = 0;
    uint32_t engagedCount   = 0;

    // Ascending member order. members is already ordered by slotIndex within a
    // squad (rebuildSquadMembers guarantees it), so this walk is stable.
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t i = members[start + k];
        if (soldiers.state[i] == SoldierState::Dead) continue;
        if (rankOfSlot(shape, soldiers.slotIndex[i], n) != 0u) continue;

        frontRankCount++;

        const float px = soldiers.posX[i];
        const float py = soldiers.posY[i];
        hash.queryNeighbors(px, py, kContactRadius, scratch);

        for (uint32_t e : scratch) {
            if ((size_t)e == (size_t)i) continue;
            if (soldiers.team[e] == ownTeam) continue;
            if (soldiers.state[e] == SoldierState::Dead) continue;
            const float dx = soldiers.posX[e] - px;
            const float dy = soldiers.posY[e] - py;
            if (dx * dx + dy * dy <= radiusSq) {
                engagedCount++;
                break;   // this member counts once, however many enemies it faces
            }
        }
    }

    // A squad whose entire front rank is dead has no front to fight with. The
    // survivors inherit slot 0 on the next rebuildSquadMembers, so this
    // resolves itself in one tick rather than needing a special case.
    bool inContactNow = false;
    if (frontRankCount > 0) {
        const float engagedFraction = (float)engagedCount / (float)frontRankCount;
        inContactNow = (engagedFraction >= kContactFraction);
    }

    if (inContactNow) {
        squads.contactTimer[s] = kContactClearSeconds;
        squads.contact[s] = 1;
    } else if (squads.contact[s]) {
        // Latched. Bleed the grace period down; clear only when it runs out.
        squads.contactTimer[s] -= dt;
        if (squads.contactTimer[s] <= 0.0f) {
            squads.contactTimer[s] = 0.0f;
            squads.contact[s] = 0;
        }
    }
}
