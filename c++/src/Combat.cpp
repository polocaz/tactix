#include "Combat.hpp"
#include "Shields.hpp"
#include "Formation.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "SpatialHash.hpp"
#include "Units.hpp"
#include <cmath>

void selectMeleeTarget(SoldierHot& soldiers, const SquadHot& squads,
                       const SpatialHash& hash, size_t i,
                       std::vector<uint32_t>& scratch) {
    soldiers.intentTarget[i] = UINT32_MAX;

    if (soldiers.state[i] == SoldierState::Dead) return;
    if (soldiers.attackCooldown[i] > 0.0f) return;

    const uint16_t sq = soldiers.squadId[i];
    if ((size_t)sq >= squads.count) return;
    const FormationShape shape = (FormationShape)squads.shape[sq];
    const FormationTraits& tr = traitsOf(shape);
    const uint32_t rank = rankOfSlot(shape, soldiers.slotIndex[i],
                                     squads.memberCount[sq]);

    // A rank inside the formation's fighting depth reaches as far as its weapon
    // allows. Every other rank keeps the base reach it always had, so a squad
    // that is flanked, or has enemies inside it, can still defend itself.
    //
    // This is deliberately NOT the cheaper rule of skipping the query entirely
    // for ranks past the fighting depth. That would remove roughly 78 percent
    // of melee queries and would also leave a squad attacked from behind unable
    // to fight back at all. Correctness first: doing it safely needs a per-squad
    // "enemy inside our ranks" flag, which is its own piece of work.
    const bool extended = rank < tr.fightingRanks;
    const float reach = extended ? kMeleeReach * tr.reach : kMeleeReach;

    const float px = soldiers.posX[i];
    const float py = soldiers.posY[i];
    hash.queryNeighbors(px, py, reach, scratch);

    float bestSq = reach * reach;
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
        if (dSq >= bestSq) continue;

        // A man reaching PAST the rank in front of him may only do so forward.
        // A spear reaches over your own front rank, never around it. Rank 0 is
        // unrestricted because he IS the front rank, and so is anything inside
        // base reach, which is the self-defence case above.
        if (rank > 0 && dSq > kMeleeReach * kMeleeReach) {
            // Negated: impactArc takes the direction a blow TRAVELS toward the
            // man being classified, and the question here is where the enemy
            // sits relative to our own facing, which is the same test reversed.
            if (impactArc(-dx, -dy, squads.facingX[sq], squads.facingY[sq])
                != ImpactArc::Front) {
                continue;
            }
        }

        bestSq = dSq;
        best = n;
    }

    soldiers.intentTarget[i] = best;
}

void applyMeleeIntents(SoldierHot& soldiers, const SquadHot& squads, const Rng& rng) {
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

        // Stage one: his shield may take it. The blow travels from the attacker
        // toward the target, which is the direction impactArc wants.
        const float ix = soldiers.posX[t] - soldiers.posX[i];
        const float iy = soldiers.posY[t] - soldiers.posY[i];
        const uint8_t blockPct = shieldBlockPct(soldiers, squads, t, ix, iy, true);
        const bool blocked =
            rng.range((uint32_t)i, RngUse::MeleeBlockRoll, 1, 100) <= blockPct;

        // Stage two: a blow that gets past the shield still has to get through
        // what he is wearing. The SIDEARM, not the weapon: a legionary inside
        // melee range has already thrown his pilum and is fighting with a sword.
        const Loadout& attacker = loadoutOf(soldiers.troopClass[i]);
        const Loadout& defender = loadoutOf(soldiers.troopClass[t]);
        const uint8_t woundPct =
            kWoundChancePct[(int)attacker.sidearm][(int)defender.armor];

        if (!blocked &&
            rng.range((uint32_t)i, RngUse::MeleeWoundRoll, 1, 100) <= woundPct) {
            soldiers.health[t] = (soldiers.health[t] > kMeleeDamage)
                               ? (uint8_t)(soldiers.health[t] - kMeleeDamage)
                               : (uint8_t)0;
        }

        // The cooldown is spent whether or not the blow landed, and that is a
        // correctness requirement rather than a detail. If a failed blow left
        // the cooldown clear, the attacker would re-roll every tick until he
        // got through, and armor would be a brief delay instead of a defense.
        // A parried swing costs you the swing.
        //
        // Scaled by the attacker's formation: testudo at 2.2 is the price of
        // its cover, because men fighting from under their shields fight badly.
        const FormationShape shape =
            (FormationShape)squads.shape[soldiers.squadId[i]];
        soldiers.attackCooldown[i] = kMeleeCooldown * traitsOf(shape).cooldown;
        soldiers.state[i] = SoldierState::Engaged;
    }

    // Intents are single-use. Clearing here means a stale index can never be
    // read on a later tick, which matters because task 5's compaction
    // renumbers soldiers.
    for (size_t i = 0; i < soldiers.count; ++i) {
        soldiers.intentTarget[i] = UINT32_MAX;
    }
}

void recordCasualties(SoldierHot& soldiers,
                      std::vector<uint32_t>& casualties,
                      std::vector<uint8_t>& officerDied) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.health[i] != 0) continue;
        if (soldiers.state[i] == SoldierState::Dead) continue;  // already counted

        soldiers.state[i] = SoldierState::Dead;

        const uint16_t sq = soldiers.squadId[i];
        if (sq < casualties.size()) {
            casualties[sq]++;
            // The officer is whoever holds slot 0. Capture it now: compaction
            // removes this corpse and the next man inherits the slot, after
            // which there is no way to tell an officer died at all.
            if (soldiers.slotIndex[i] == 0) {
                officerDied[sq] = 1;
            }
        }
    }
}

void compactDead(SoldierHot& soldiers,
                 std::vector<float>& prevPosX,
                 std::vector<float>& prevPosY) {
    size_t i = 0;
    while (i < soldiers.count) {
        if (soldiers.state[i] != SoldierState::Dead) {
            ++i;
            continue;
        }

        const size_t last = soldiers.count - 1;
        if (i != last) {
            // Swap-with-back. Every parallel array must move together or the
            // structure of arrays desyncs.
            soldiers.posX[i]            = soldiers.posX[last];
            soldiers.posY[i]            = soldiers.posY[last];
            soldiers.velX[i]            = soldiers.velX[last];
            soldiers.velY[i]            = soldiers.velY[last];
            soldiers.dirX[i]            = soldiers.dirX[last];
            soldiers.dirY[i]            = soldiers.dirY[last];
            soldiers.team[i]            = soldiers.team[last];
            soldiers.unitType[i]        = soldiers.unitType[last];
            soldiers.troopClass[i]      = soldiers.troopClass[last];
            soldiers.state[i]           = soldiers.state[last];
            soldiers.squadId[i]         = soldiers.squadId[last];
            soldiers.slotIndex[i]       = soldiers.slotIndex[last];
            soldiers.health[i]          = soldiers.health[last];
            soldiers.attackCooldown[i]  = soldiers.attackCooldown[last];
            soldiers.intentTarget[i]    = soldiers.intentTarget[last];
            soldiers.intentFire[i]      = soldiers.intentFire[last];
            soldiers.steadyTimer[i]     = soldiers.steadyTimer[last];
            prevPosX[i]                 = prevPosX[last];
            prevPosY[i]                 = prevPosY[last];
            // Do NOT advance i: the soldier just swapped in has not been
            // examined yet and may itself be dead.
        } else {
            ++i;
        }

        soldiers.posX.pop_back();
        soldiers.posY.pop_back();
        soldiers.velX.pop_back();
        soldiers.velY.pop_back();
        soldiers.dirX.pop_back();
        soldiers.dirY.pop_back();
        soldiers.team.pop_back();
        soldiers.unitType.pop_back();
        soldiers.troopClass.pop_back();
        soldiers.state.pop_back();
        soldiers.squadId.pop_back();
        soldiers.slotIndex.pop_back();
        soldiers.health.pop_back();
        soldiers.attackCooldown.pop_back();
        soldiers.intentTarget.pop_back();
        soldiers.intentFire.pop_back();
        soldiers.steadyTimer.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
        soldiers.count--;
    }
}
