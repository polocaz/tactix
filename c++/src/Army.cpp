#include "Army.hpp"
#include "Squads.hpp"
#include <algorithm>
#include <cmath>

float squadStrength(const SquadHot& squads, size_t s) {
    return (float)squads.memberCount[s] * kStrengthPerMan[(int)squads.unitType[s]];
}

void updateArmyAggregate(const SquadHot& squads, ArmyHot& armies) {
    for (size_t a = 0; a < armies.count; ++a) {
        armies.strengthInfantry[a] = 0.0f;
        armies.strengthArcher[a]   = 0.0f;
        armies.strengthCavalry[a]  = 0.0f;
    }

    // Accumulated in ascending squad order on one thread, so the result does
    // not depend on how anything upstream was chunked.
    float sumX[2]      = { 0.0f, 0.0f };
    float sumY[2]      = { 0.0f, 0.0f };
    float weight[2]    = { 0.0f, 0.0f };
    float infSumX[2]   = { 0.0f, 0.0f };
    float infSumY[2]   = { 0.0f, 0.0f };
    float infWeight[2] = { 0.0f, 0.0f };

    for (size_t s = 0; s < squads.count; ++s) {
        if (squads.memberCount[s] == 0) continue;
        const size_t a = (size_t)squads.team[s];
        if (a >= armies.count) continue;

        const float men = (float)squads.memberCount[s];
        switch (squads.unitType[s]) {
            case UnitType::Archer:  armies.strengthArcher[a]   += men; break;
            case UnitType::Cavalry: armies.strengthCavalry[a]  += men; break;
            default:                armies.strengthInfantry[a] += men; break;
        }

        sumX[a] += squads.centroidX[s] * men;
        sumY[a] += squads.centroidY[s] * men;
        weight[a] += men;

        // The front is the INFANTRY mean, not the whole-army mean. Archers
        // stand well back, and letting them drag the front line rearward would
        // make "behind our line" a place that is already behind the archers.
        if (squads.unitType[s] == UnitType::Infantry) {
            infSumX[a] += squads.centroidX[s] * men;
            infSumY[a] += squads.centroidY[s] * men;
            infWeight[a] += men;
        }
    }

    for (size_t a = 0; a < armies.count; ++a) {
        if (weight[a] > 0.0f) {
            armies.centroidX[a] = sumX[a] / weight[a];
            armies.centroidY[a] = sumY[a] / weight[a];
        }
        // An annihilated army keeps its last centroid rather than going to
        // the world origin or to NaN. Armies are never destroyed, so anything
        // still reading this must see a sane value.

        if (infWeight[a] > 0.0f) {
            armies.frontX[a] = infSumX[a] / infWeight[a];
            armies.frontY[a] = infSumY[a] / infWeight[a];
        } else {
            // No infantry left: an army of archers and horse has no shield
            // wall, so its "front" is simply where it is.
            armies.frontX[a] = armies.centroidX[a];
            armies.frontY[a] = armies.centroidY[a];
        }
    }

    // Front direction, computed after every front is final so each army can
    // read the other's.
    for (size_t a = 0; a < armies.count; ++a) {
        const size_t other = (a == 0) ? 1u : 0u;
        if (other >= armies.count) continue;
        const float dx = armies.centroidX[other] - armies.centroidX[a];
        const float dy = armies.centroidY[other] - armies.centroidY[a];
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-6f) {
            armies.frontDirX[a] = dx / len;
            armies.frontDirY[a] = dy / len;
        }
        // else: keep the previous direction, which spawn() seeded to (1,0), so
        // this is never zero length and never NaN.
    }
}

namespace {

bool isMelee(UnitType u) { return u != UnitType::Archer; }

float distBetween(const SquadHot& q, size_t a, size_t b) {
    const float dx = q.centroidX[b] - q.centroidX[a];
    const float dy = q.centroidY[b] - q.centroidY[a];
    return std::sqrt(dx * dx + dy * dy);
}

// Nearest live enemy squad to `s`, or UINT16_MAX if the enemy is annihilated.
// Ascending walk with strict less-than, so equidistant enemies resolve to the
// lowest index on every platform.
uint16_t nearestEnemy(const SquadHot& q, size_t s) {
    uint16_t best = UINT16_MAX;
    float bestDist = 1e30f;
    for (size_t e = 0; e < q.count; ++e) {
        if (q.team[e] == q.team[s]) continue;
        if (q.memberCount[e] == 0) continue;
        const float d = distBetween(q, s, e);
        if (d < bestDist) { bestDist = d; best = (uint16_t)e; }
    }
    return best;
}

} // namespace

void assignRoles(SquadHot& squads, const ArmyHot& armies, Team team) {
    (void)armies;   // aggregates are read by the role anchors, not by assignment

    // Live squads of each side, in ascending index order.
    std::vector<uint16_t> own, foe;
    own.reserve(squads.count);
    foe.reserve(squads.count);
    for (size_t s = 0; s < squads.count; ++s) {
        if (squads.memberCount[s] == 0) continue;
        if (squads.team[s] == team) own.push_back((uint16_t)s);
        else                        foe.push_back((uint16_t)s);
    }

    // Nothing left to fight. Leave every role and target as it stands rather
    // than inventing an assignment against a dead enemy.
    if (own.empty() || foe.empty()) return;

    // Step 1: every squad starts unassigned, with the nearest live enemy as a
    // default target. Later steps override the target where they have a better
    // opinion, so no squad can ever come out of this without one.
    for (uint16_t s : own) {
        squads.role[s] = (uint8_t)SquadRole::Reserve;
        squads.wardSquad[s] = UINT16_MAX;
        const uint16_t n = nearestEnemy(squads, s);
        if (n != UINT16_MAX) squads.targetSquad[s] = n;
    }

    // Step 2: archers shoot.
    for (uint16_t s : own) {
        if (squads.unitType[s] == UnitType::Archer) {
            squads.role[s] = (uint8_t)SquadRole::Shoot;
        }
    }

    // Step 3: each threatened archer squad claims at most ONE infantry squad
    // as its screen. The cap is what stops the whole army becoming
    // bodyguards; walking archers in ascending index makes which archer gets
    // the last spare infantry squad deterministic.
    std::vector<uint8_t> claimed(squads.count, 0u);
    for (uint16_t a : own) {
        if (squads.unitType[a] != UnitType::Archer) continue;

        uint16_t threat = UINT16_MAX;
        float threatDist = 1e30f;
        for (uint16_t e : foe) {
            if (!isMelee(squads.unitType[e])) continue;
            const float d = distBetween(squads, a, e);
            if (d < threatDist) { threatDist = d; threat = e; }
        }
        if (threat == UINT16_MAX || threatDist > kScreenThreatRadius) continue;

        uint16_t guard = UINT16_MAX;
        float guardDist = 1e30f;
        for (uint16_t g : own) {
            if (squads.unitType[g] != UnitType::Infantry) continue;
            if (claimed[g]) continue;
            const float d = distBetween(squads, a, g);
            if (d < guardDist) { guardDist = d; guard = g; }
        }
        if (guard == UINT16_MAX) continue;

        claimed[guard] = 1;
        squads.role[guard] = (uint8_t)SquadRole::Screen;
        squads.wardSquad[guard] = a;
        squads.targetSquad[guard] = threat;
    }

    // Step 4: cavalry flank the most exposed enemy, defined as the one whose
    // nearest friendly (enemy-side) support is farthest away. The cheapest
    // definition of an exposed flank that is not simply "nearest".
    for (uint16_t c : own) {
        if (squads.unitType[c] != UnitType::Cavalry) continue;

        uint16_t pick = UINT16_MAX;
        float bestExposure = -1.0f;
        for (uint16_t e : foe) {
            float support = 1e30f;
            for (uint16_t o : foe) {
                if (o == e) continue;
                support = std::min(support, distBetween(squads, e, o));
            }
            // A lone enemy squad has no support at all, so it is maximally
            // exposed. Capped rather than left at 1e30 so the comparison below
            // stays meaningful when several enemies are alone.
            if (support > 1e29f) support = 1e6f;
            if (support > bestExposure) { bestExposure = support; pick = e; }
        }
        if (pick != UINT16_MAX) {
            squads.role[c] = (uint8_t)SquadRole::Flank;
            squads.targetSquad[c] = pick;
        }
    }

    // Step 5: remaining infantry form the line, spread over enemy squads by
    // greedy lowest-load where each enemy's demand is its strength. THIS is
    // the target spreading: without it every squad picks the nearest enemy and
    // one forward enemy squad draws the whole army.
    std::vector<float> load(squads.count, 0.0f);
    std::vector<float> demand(squads.count, 0.0f);
    for (uint16_t e : foe) {
        demand[e] = std::max(squadStrength(squads, e), 1.0f);
    }

    for (uint16_t s : own) {
        if (squads.role[s] != (uint8_t)SquadRole::Reserve) continue;
        if (squads.unitType[s] != UnitType::Infantry) continue;

        const float ourStrength = std::max(squadStrength(squads, s), 1.0f);

        uint16_t pick = UINT16_MAX;
        float bestRatio = 1e30f;
        for (uint16_t e : foe) {
            // Fill the least-covered enemy first. Ties break on the lower
            // enemy index because the walk is ascending and the test is
            // strict less-than.
            const float ratio = (load[e] + ourStrength) / demand[e];
            if (ratio < bestRatio) { bestRatio = ratio; pick = e; }
        }
        if (pick == UINT16_MAX) continue;

        load[pick] += ourStrength;
        squads.role[s] = (uint8_t)SquadRole::Line;
        squads.targetSquad[s] = pick;
    }

    // Step 6: anything still unassigned stays Reserve, with the default
    // nearest-enemy target step 1 gave it.
}
