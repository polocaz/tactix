#pragma once
#include "Rng.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Formation.hpp"
#include "Shields.hpp"
#include "Units.hpp"
#include <cstdint>
#include <limits>
#include <vector>

class SpatialHash;

// The per-soldier half of selectMeleeTarget, split from the grid walk that
// feeds it.
//
// The split exists so the steering phase can drive this from the SAME walk
// separation already performs. Both used to ask the grid for neighbours at the
// identical position, and queryNeighbors ignores its radius argument and always
// returns the fixed 3x3 block, so the second walk provably returned the first
// walk's list. It was repeated work, not a second question.
//
// Constructing this is what decides whether the soldier is looking for a target
// at all; a soldier who is dead, still on cooldown, or squadless leaves
// intentTarget at UINT32_MAX exactly as the early returns used to.
struct MeleeTargetSearch {
    MeleeTargetSearch(const SoldierHot& soldiers, const SquadHot& squads, size_t i)
        : self(i)
    {
        if (soldiers.state[i] == SoldierState::Dead) return;
        if (soldiers.attackCooldown[i] > 0.0f) return;

        const uint16_t sq = soldiers.squadId[i];
        if ((size_t)sq >= squads.count) return;

        const FormationShape shape = (FormationShape)squads.shape[sq];
        const FormationTraits& tr = traitsOf(shape);
        rank = rankOfSlot(shape, soldiers.slotIndex[i], squads.memberCount[sq]);

        // A rank inside the formation's fighting depth reaches as far as its
        // weapon allows. Every other rank keeps the base reach it always had,
        // so a squad that is flanked, or has enemies inside it, can still
        // defend itself.
        const bool extended = rank < tr.fightingRanks;
        reach = extended ? kMeleeReach * tr.reach : kMeleeReach;

        bestSq  = reach * reach;
        px      = soldiers.posX[i];
        py      = soldiers.posY[i];
        team    = soldiers.team[i];
        facingX = squads.facingX[sq];
        facingY = squads.facingY[sq];
        searching = true;
    }

    // True when this soldier can take a target at all. A caller that owns the
    // walk may use it to skip work; consider() is safe either way.
    bool isSearching() const { return searching; }

    // Radius a standalone walk should ask for. queryNeighbors ignores it, but
    // passing the real number keeps the call honest about what it wants.
    float queryRadius() const { return reach; }

    void consider(const SoldierHot& soldiers, uint32_t n) {
        if (!searching) return;
        if ((size_t)n == self) return;
        if (soldiers.team[n] == team) return;
        if (soldiers.state[n] == SoldierState::Dead) return;

        const float dx = soldiers.posX[n] - px;
        const float dy = soldiers.posY[n] - py;
        const float dSq = dx * dx + dy * dy;

        // Strictly-less keeps the FIRST of any equidistant pair, and the grid
        // walks cells in a fixed order over insertion-ordered vectors, so the
        // winner is the same on every thread and platform.
        if (dSq >= bestSq) return;

        // A man reaching PAST the rank in front of him may only do so forward.
        // A spear reaches over your own front rank, never around it. Rank 0 is
        // unrestricted because he IS the front rank, and so is anything inside
        // base reach, which is the self-defence case above.
        if (rank > 0 && dSq > kMeleeReach * kMeleeReach) {
            // Negated: impactArc takes the direction a blow TRAVELS toward the
            // man being classified, and the question here is where the enemy
            // sits relative to our own facing, which is the same test reversed.
            if (impactArc(-dx, -dy, facingX, facingY) != ImpactArc::Front) return;
        }

        bestSq = dSq;
        best = n;
    }

    // Writes the soldier's own slot and nothing else, so this stays
    // parallel-safe. A search that never started commits UINT32_MAX, which is
    // what the original wrote before its early returns.
    void commit(SoldierHot& soldiers) const { soldiers.intentTarget[self] = best; }

private:
    size_t   self;
    uint32_t best = std::numeric_limits<uint32_t>::max();
    uint32_t rank = 0;
    float    reach = kMeleeReach;
    float    bestSq = 0.0f;
    float    px = 0.0f, py = 0.0f;
    float    facingX = 0.0f, facingY = 0.0f;
    Team     team = Team::A;
    bool     searching = false;
};

// Chooses an enemy in reach and records it in the soldier's own intentTarget.
// Parallel-safe: writes only soldierIndex's own slot, and reads only positions,
// teams and the squad tier, none of which a parallel phase writes.
//
// Reach depends on the formation. A rank inside its fighting depth reaches
// kMeleeReach times the shape's reach scale, and only into the squad's front
// arc; every other rank keeps base reach in every direction, so a flanked squad
// can still fight.
//
// Ties are broken by lowest soldier index, so the choice is identical at any
// thread count and on any platform.
//
// This owns its grid walk. The steering phase does NOT call it: it drives
// MeleeTargetSearch directly off the walk it already runs for separation.
void selectMeleeTarget(SoldierHot& soldiers, const SquadHot& squads,
                       const SpatialHash& hash, size_t soldierIndex,
                       std::vector<uint32_t>& scratch);

// Resolution step 1 (spec 5.5). Applies every soldier's melee intent in
// ascending soldier index order. Single-threaded: this is the only place a
// soldier may write another soldier's health.
//
// A blow that reaches does not automatically wound: it rolls against what the
// target is wearing (kWoundChancePct). `squads` supplies the formation the
// attacker is standing in, which scales his swing rate.
void applyMeleeIntents(SoldierHot& soldiers, const SquadHot& squads, const Rng& rng);

// Resolution step 4 (spec 5.5). Runs BEFORE compaction, because the officer is
// identified by slotIndex 0 and compaction reassigns slots.
void recordCasualties(SoldierHot& soldiers,
                      std::vector<uint32_t>& casualties,
                      std::vector<uint8_t>& officerDied);

// Resolution step 6 (spec 5.5). Swap-with-back removal of Dead soldiers. This
// INVALIDATES every soldier index, so it must run after every step that reads
// one.
void compactDead(SoldierHot& soldiers,
                 std::vector<float>& prevPosX,
                 std::vector<float>& prevPosY);
