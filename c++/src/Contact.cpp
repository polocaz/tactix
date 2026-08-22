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

void resolveOverlap(SoldierHot& soldiers,
                    const std::vector<float>& nextX,
                    const std::vector<float>& nextY,
                    const SpatialHash& hash, size_t i,
                    std::vector<uint32_t>& scratch) {
    const float px = nextX[i];
    const float py = nextY[i];

    if (soldiers.state[i] == SoldierState::Dead) {
        soldiers.posX[i] = px;
        soldiers.posY[i] = py;
        return;
    }

    // 2 * kSoldierRadius is 8px. That sits below kSeparationRadius (10px),
    // which sits below kSlotSpacing (12px), so a soldier standing correctly on
    // its slot feels zero force from any of the three. Same layering argument
    // kSeparationRadius's own comment makes, extended by one term: it is why
    // adding hard collision does not fight held formations.
    const float minDist = 2.0f * kSoldierRadius;
    const float minDistSq = minDist * minDist;

    // The hash was built from posX/posY at the top of the tick, so it is one
    // movement step stale here. At maxSpeed (150 px/s) and 60 Hz that is 2.5px
    // against 50px cells, so a 3x3 query still finds everyone within 8px. A
    // documented tolerance, and it saves a full rebuild.
    hash.queryNeighbors(px, py, minDist, scratch);

    float dx = 0.0f;
    float dy = 0.0f;
    uint32_t contacts = 0;

    for (uint32_t n : scratch) {
        if ((size_t)n == i) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;

        const float ox = px - nextX[n];
        const float oy = py - nextY[n];
        const float dSq = ox * ox + oy * oy;
        if (dSq >= minDistSq) continue;

        if (dSq < 1e-6f) {
            // Exactly coincident, so there is no separating axis to use. Break
            // the tie on index: i pushes +x when its neighbour's index is
            // higher, and that neighbour's own call sees a LOWER index and
            // pushes -x. Opposite by construction, and identical on every
            // thread and platform.
            dx += (n > (uint32_t)i ? 1.0f : -1.0f) * kSoldierRadius;
            contacts++;
            continue;
        }

        const float d = std::sqrt(dSq);
        const float push = (minDist - d) * 0.5f;
        dx += (ox / d) * push;
        dy += (oy / d) * push;
        contacts++;
    }

    // Corrections are SUMMED, then the total is clamped. Three variants were
    // measured over a 600-tick 2000-agent battle, counting pairs left more than
    // half overlapped:
    //
    //   sum, clamped          142 pairs, closest 0.30px   (this)
    //   sum, unclamped        168 pairs, closest 0.40px
    //   sum over two passes    98 pairs, closest 0.95px   (double the cost)
    //   average by contacts   267 pairs, closest 0.75px
    //
    // A residue survives all of them, and it is not an iteration count problem:
    // in a dense scrum a soldier is boxed in on every side, its neighbours'
    // pushes cancel, and there is nowhere for it to go. That is arguably what a
    // crush should look like. The residue is bounded and tested rather than
    // asserted away; see test_contact.cpp's full-battle case.
    //
    // Averaging is the textbook fix for a Jacobi solver overshooting on k
    // simultaneous constraints, and it is worse here: it under-corrects in
    // exactly the dense scrum where separation matters, because opposing
    // neighbours cancel before the divisor is even applied. Summing pushes
    // harder and separates more pairs, so summing is what this keeps.
    //
    // The clamp bounds the overshoot summing can produce without weakening the
    // push: no single tick may displace a soldier further than one body
    // radius, which is well beyond what any real separation needs and far
    // short of the flings that unclamped summing can produce when a soldier is
    // boxed in on several sides.
    //
    // A single contact is unaffected in every variant, so the exact-separation
    // property the pair test asserts holds regardless.
    (void)contacts;
    const float dLenSq = dx * dx + dy * dy;
    const float maxStep = kSoldierRadius;
    if (dLenSq > maxStep * maxStep) {
        const float scale = maxStep / std::sqrt(dLenSq);
        dx *= scale;
        dy *= scale;
    }

    soldiers.posX[i] = px + dx;
    soldiers.posY[i] = py + dy;
}
