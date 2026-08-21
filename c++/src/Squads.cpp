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

    // Facing is derived from the order's objective (spec 6.6), but NOT here:
    // this function runs in phase 2, parallel across squads, while every
    // squad's own centroid above is still being written by its own
    // concurrent job. Reading another squad's centroid at that point would
    // race with that squad's write. selectTargetSquad below does the same
    // derivation safely, in phase 3, after phase 2's barrier has made every
    // centroid read-only for the rest of the tick.
    normalizeFacing(squads, s);
}

void selectTargetSquad(SquadHot& squads, size_t s) {
    if (squads.memberCount[s] == 0) return;

    float bestDistSq = 1e30f;
    uint16_t best = squads.targetSquad[s];
    bool found = false;

    // Walked in ascending index order so ties resolve identically on every
    // thread and platform.
    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;
        if (d < bestDistSq) {
            bestDistSq = d;
            best = (uint16_t)e;
            found = true;
        }
    }

    if (found) {
        squads.targetSquad[s] = best;
        squads.order[s] = (uint8_t)SquadOrder::Advance;

        // Spec 6.6: facing comes from the order's objective, not from
        // averaging soldier directions (noisy for a loose formation) and not
        // from centroid velocity (undefined when stationary). Safe here,
        // unlike in updateSquadAggregate above: phase 3 runs after phase 2's
        // barrier, so every squad's centroid -- including the target's -- is
        // finalized and read-only for the rest of the tick, and this writes
        // only squad s's own facing.
        const float dx = squads.centroidX[best] - squads.centroidX[s];
        const float dy = squads.centroidY[best] - squads.centroidY[s];
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-6f) {
            squads.facingX[s] = dx / len;
            squads.facingY[s] = dy / len;
        }
    } else {
        // Every enemy squad is wiped out. Hold rather than advancing on a
        // stale target; keep the last facing.
        squads.order[s] = (uint8_t)SquadOrder::Hold;
    }
}

void normalizeFacing(SquadHot& squads, size_t s) {
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
