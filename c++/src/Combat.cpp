#include "Combat.hpp"
#include "Simulation.hpp"
#include "SpatialHash.hpp"
#include "Units.hpp"
#include <cmath>

void selectMeleeTarget(SoldierHot& soldiers, const SpatialHash& hash,
                       size_t i, std::vector<uint32_t>& scratch) {
    soldiers.intentTarget[i] = UINT32_MAX;

    if (soldiers.state[i] == SoldierState::Dead) return;
    if (soldiers.attackCooldown[i] > 0.0f) return;

    const float px = soldiers.posX[i];
    const float py = soldiers.posY[i];
    hash.queryNeighbors(px, py, kMeleeReach, scratch);

    const float reachSq = kMeleeReach * kMeleeReach;
    float bestSq = reachSq;
    uint32_t best = UINT32_MAX;

    for (uint32_t n : scratch) {
        if ((size_t)n == i) continue;
        if (soldiers.team[n] == soldiers.team[i]) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;

        const float dx = soldiers.posX[n] - px;
        const float dy = soldiers.posY[n] - py;
        const float dSq = dx * dx + dy * dy;

        // Strictly-less keeps the FIRST of any equidistant pair, and
        // queryNeighbors walks cells in a fixed order over insertion-ordered
        // vectors, so the winner is the same on every thread and platform.
        if (dSq < bestSq) {
            bestSq = dSq;
            best = n;
        }
    }

    soldiers.intentTarget[i] = best;
}
