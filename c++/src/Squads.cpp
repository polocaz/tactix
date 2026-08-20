#include "Squads.hpp"
#include "Simulation.hpp"
#include <algorithm>

void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members) {
    const size_t squadCount = squads.count;

    // Counting pass. Dead soldiers are excluded so that a squad's range holds
    // only live members; plan 2's compaction removes them from the array.
    std::vector<uint32_t> counts(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        counts[soldiers.squadId[i]]++;
    }

    uint32_t running = 0;
    for (size_t s = 0; s < squadCount; ++s) {
        squads.memberStart[s] = running;
        squads.memberCount[s] = counts[s];
        running += counts[s];
    }

    members.assign(running, 0u);
    std::vector<uint32_t> cursor(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        const uint16_t s = soldiers.squadId[i];
        members[squads.memberStart[s] + cursor[s]++] = (uint32_t)i;
    }

    // Order each squad's range by previous slotIndex. Keys are unique within
    // a squad, so std::sort is deterministic here despite not being stable.
    for (size_t s = 0; s < squadCount; ++s) {
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        std::sort(members.begin() + start, members.begin() + start + n,
                  [&soldiers](uint32_t a, uint32_t b) {
                      return soldiers.slotIndex[a] < soldiers.slotIndex[b];
                  });
        for (uint32_t k = 0; k < n; ++k) {
            soldiers.slotIndex[members[start + k]] = (uint16_t)k;
        }
    }
}
