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
