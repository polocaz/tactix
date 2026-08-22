#include "Contact.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "SpatialHash.hpp"
#include "Formation.hpp"
#include <cmath>

void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t s, float dt,
                   std::vector<uint32_t>& scratch) {
    // Captured BEFORE the flag is recomputed below, so the rising and falling
    // edges are both detectable from one pass.
    const uint8_t wasInContact = squads.contact[s];

    const uint32_t start = squads.memberStart[s];
    const uint32_t n     = squads.memberCount[s];

    if (n == 0) {
        squads.contact[s] = 0;
        squads.contactTimer[s] = 0.0f;
        // An emptied squad keeps its anchor where it stands. Squads are never
        // destroyed, so anything still reading this must see a sane value.
        return;
    }

    const FormationShape shape = shapeForUnit(squads.unitType[s]);
    const Team ownTeam = squads.team[s];
    const float radiusSq = kContactRadius * kContactRadius;

    uint32_t frontRankCount = 0;
    uint32_t engagedCount   = 0;

    // Ascending member order. members is already ordered by slotIndex within a
    // squad (rebuildSquadMembers guarantees it), so this walk is stable.
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t i = members[start + k];
        if (soldiers.state[i] == SoldierState::Dead) continue;
        if (rankOfSlot(shape, soldiers.slotIndex[i], n) != 0u) continue;

        frontRankCount++;

        const float px = soldiers.posX[i];
        const float py = soldiers.posY[i];
        hash.queryNeighbors(px, py, kContactRadius, scratch);

        for (uint32_t e : scratch) {
            if ((size_t)e == (size_t)i) continue;
            if (soldiers.team[e] == ownTeam) continue;
            if (soldiers.state[e] == SoldierState::Dead) continue;
            const float dx = soldiers.posX[e] - px;
            const float dy = soldiers.posY[e] - py;
            if (dx * dx + dy * dy <= radiusSq) {
                engagedCount++;
                break;   // this member counts once, however many enemies it faces
            }
        }
    }

    // A squad whose entire front rank is dead has no front to fight with. The
    // survivors inherit slot 0 on the next rebuildSquadMembers, so this
    // resolves itself in one tick rather than needing a special case.
    bool inContactNow = false;
    if (frontRankCount > 0) {
        const float engagedFraction = (float)engagedCount / (float)frontRankCount;
        inContactNow = (engagedFraction >= kContactFraction);
    }

    if (inContactNow) {
        squads.contactTimer[s] = kContactClearSeconds;
        squads.contact[s] = 1;
    } else if (squads.contact[s]) {
        // Latched. Bleed the grace period down; clear only when it runs out.
        squads.contactTimer[s] -= dt;
        if (squads.contactTimer[s] <= 0.0f) {
            squads.contactTimer[s] = 0.0f;
            squads.contact[s] = 0;
        }
    }

    // --- Anchor maintenance (design 5.2) ---
    // Free:      the anchor IS the centroid, so an unengaged squad behaves
    //            exactly as it did before anchoring existed.
    // Latching:  on the rising edge, freeze where we stand.
    // Engaged:   hold, whatever the centroid does. Cutting this link is the
    //            fix for the centroid/slot feedback loop (design 2.1).
    // Releasing: ease back, so the formation does not teleport by however far
    //            the centroid drifted during the fight.
    if (squads.contact[s]) {
        if (!wasInContact) {
            // Rising edge: freeze where we stand.
            squads.anchorX[s] = squads.centroidX[s];
            squads.anchorY[s] = squads.centroidY[s];
        }
        // else: hold the latched anchor, whatever the centroid does. Cutting
        // this link is the fix for the centroid/slot feedback loop.
        squads.anchorReleaseTimer[s] = 0.0f;
    } else if (wasInContact || squads.anchorReleaseTimer[s] > 0.0f) {
        // Releasing. Ease back over kAnchorReleaseSeconds so the formation
        // does not teleport by however far the centroid drifted during the
        // fight.
        if (wasInContact) squads.anchorReleaseTimer[s] = kAnchorReleaseSeconds;

        const float k = dt / kAnchorReleaseSeconds;
        squads.anchorX[s] += (squads.centroidX[s] - squads.anchorX[s]) * k;
        squads.anchorY[s] += (squads.centroidY[s] - squads.anchorY[s]) * k;

        squads.anchorReleaseTimer[s] -= dt;
        if (squads.anchorReleaseTimer[s] <= 0.0f) {
            // Ease over. Snap the remaining error away rather than trailing
            // forever, and hand the squad back to exact tracking below.
            squads.anchorReleaseTimer[s] = 0.0f;
            squads.anchorX[s] = squads.centroidX[s];
            squads.anchorY[s] = squads.centroidY[s];
        }
    } else {
        // Free: the anchor IS the centroid, exactly. This is what makes an
        // unengaged squad behave precisely as it did before anchoring existed.
        //
        // Easing here instead would be a real bug rather than a nicety: a
        // marching squad's centroid moves about 0.75px per tick, so an ease at
        // dt/kAnchorReleaseSeconds would settle to a permanent trailing error
        // of roughly 22px and drag every formation slot backward with it.
        squads.anchorX[s] = squads.centroidX[s];
        squads.anchorY[s] = squads.centroidY[s];
    }
}
