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

void applyMeleeIntents(SoldierHot& soldiers) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        const uint32_t t = soldiers.intentTarget[i];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;

        // Both ends must still be alive. The attacker may have been killed
        // earlier in this same loop by a lower-indexed soldier, and the target
        // may already have been finished off. Dropping the intent in either
        // case is what makes overkill wasted rather than carried over, and it
        // is also what stops health underflowing past zero.
        if (soldiers.health[i] == 0) continue;
        if (soldiers.health[t] == 0) continue;

        soldiers.health[t] = (soldiers.health[t] > kMeleeDamage)
                           ? (uint8_t)(soldiers.health[t] - kMeleeDamage)
                           : (uint8_t)0;
        soldiers.attackCooldown[i] = kMeleeCooldown;
        soldiers.state[i] = SoldierState::Engaged;
    }

    // Intents are single-use. Clearing here means a stale index can never be
    // read on a later tick, which matters because task 5's compaction
    // renumbers soldiers.
    for (size_t i = 0; i < soldiers.count; ++i) {
        soldiers.intentTarget[i] = UINT32_MAX;
    }
}
