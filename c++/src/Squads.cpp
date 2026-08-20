#include "Squads.hpp"
#include "Simulation.hpp"
#include <algorithm>
#include <cmath>

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

    // Order each squad's range by previous slotIndex. Keys are NOT unique
    // within a squad -- every soldier spawns with slotIndex 0, so on the
    // first rebuild an entire squad ties. std::sort is introsort, not a
    // stable sort: on an all-equal range its output permutation is
    // implementation-defined, and libstdc++ and MSVC STL do not agree on it.
    // Tie-breaking on the soldier index (unique by construction) makes the
    // comparator a strict total order, so the sorted permutation is the same
    // on every platform regardless of algorithm.
    for (size_t s = 0; s < squadCount; ++s) {
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        std::sort(members.begin() + start, members.begin() + start + n,
                  [&soldiers](uint32_t a, uint32_t b) {
                      if (soldiers.slotIndex[a] != soldiers.slotIndex[b]) {
                          return soldiers.slotIndex[a] < soldiers.slotIndex[b];
                      }
                      return a < b;
                  });
        for (uint32_t k = 0; k < n; ++k) {
            soldiers.slotIndex[members[start + k]] = (uint16_t)k;
        }
    }
}

void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t s) {
    const uint32_t start = squads.memberStart[s];
    const uint32_t n = squads.memberCount[s];

    // An emptied squad keeps its last centroid. Squads are never destroyed
    // (that is what keeps targetSquad valid without a liveness check), so a
    // wiped-out squad must not poison the field with NaN.
    if (n > 0) {
        // Summed in member order on one thread, so the result is
        // bit-reproducible regardless of worker count.
        float sumX = 0.0f, sumY = 0.0f;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t i = members[start + k];
            sumX += soldiers.posX[i];
            sumY += soldiers.posY[i];
        }
        squads.centroidX[s] = sumX / (float)n;
        squads.centroidY[s] = sumY / (float)n;
    }

    // Plan 3 derives facing from the order objective. Until then a squad
    // holds its deployed facing; renormalize so formation rotation in Task 8
    // can assume unit length.
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len > 1e-6f) {
        squads.facingX[s] = fx / len;
        squads.facingY[s] = fy / len;
    } else {
        squads.facingX[s] = 1.0f;
        squads.facingY[s] = 0.0f;
    }
}
