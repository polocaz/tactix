#include "Morale.hpp"
#include "Squads.hpp"

namespace {
float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace

void updateMorale(SquadHot& squads,
                  const std::vector<uint32_t>& casualties,
                  const std::vector<uint8_t>& officerDied,
                  float dt) {
    for (size_t s = 0; s < squads.count; ++s) {
        // A wiped-out squad keeps its last morale rather than being driven to
        // a meaningless value. Squads are never destroyed, so an emptied one
        // stays in the array forever and must not poison anything reading it.
        if (squads.memberCount[s] == 0) continue;

        const uint32_t dead = (s < casualties.size()) ? casualties[s] : 0u;
        const uint8_t lostOfficer = (s < officerDied.size()) ? officerDied[s] : (uint8_t)0;

        // Size BEFORE this tick's losses: memberCount was rebuilt from the
        // survivors, so the dead are no longer in it. Losing 2 of 10 has to
        // read as a fifth of the squad, not as a quarter of what is left.
        const float sizeBefore = (float)squads.memberCount[s] + (float)dead;
        const float lossFraction = (sizeBefore > 0.0f) ? (float)dead / sizeBefore : 0.0f;

        float drop = lossFraction * kMoraleLossPerCasualtyFraction;
        if (lostOfficer) drop += kMoraleOfficerDeathPenalty;
        if (squads.rearThreat[s]) drop += kMoraleRearThreatPerSecond * dt;

        // Discipline blunts the whole loss, not just part of it. At discipline
        // 1.0 a squad takes half the morale damage of a discipline-0 rabble.
        drop *= (1.0f - squads.discipline[s] * 0.5f);

        const float recovery = kMoraleRecoveryPerSecond * dt
                             * (0.5f + squads.discipline[s] * 0.5f);

        squads.morale[s] = clamp01(squads.morale[s] - drop + recovery);
    }
}

void applyRoutTransitions(SquadHot& squads, float dt) {
    for (size_t s = 0; s < squads.count; ++s) {
        const bool routing = (squads.order[s] == (uint8_t)SquadOrder::Rout);

        if (!routing) {
            // A disciplined squad holds at a morale a levy would break at.
            const float threshold = kBaseRoutThreshold
                                  * (1.0f - squads.discipline[s] * 0.5f);
            if (squads.morale[s] < threshold && squads.memberCount[s] > 0) {
                squads.order[s] = (uint8_t)SquadOrder::Rout;
                squads.rallyTimer[s] = 0.0f;
                // A formation that has broken is not holding a line. Releasing
                // the anchor lets the squad actually run rather than orbiting
                // the point it was latched to.
                squads.contact[s] = 0;
                squads.contactTimer[s] = 0.0f;
            }
            continue;
        }

        // Routing. Rally needs sustained safety, not an instant of it, so any
        // enemy inside kRallyRadius resets the clock rather than pausing it.
        if (squads.nearestEnemyDist[s] > kRallyRadius) {
            squads.rallyTimer[s] += dt;
        } else {
            squads.rallyTimer[s] = 0.0f;
        }

        if (squads.morale[s] >= kRallyThreshold &&
            squads.rallyTimer[s] >= kRallyDuration) {
            // Hold, not Advance: a squad that has just rallied re-enters
            // normal scoring on its next decide rather than charging straight
            // back into whatever broke it.
            squads.order[s] = (uint8_t)SquadOrder::Hold;
            squads.rallyTimer[s] = 0.0f;
        }
    }
}
