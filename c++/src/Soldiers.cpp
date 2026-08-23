#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "Formation.hpp"
#include <cmath>

float squadCompression(const SquadHot& squads, size_t s) {
    // Both inputs are clamped 0..1 by Morale.cpp, but clamp defensively here
    // too: this feeds slot positions, and a value outside the range would
    // either invert the formation or fling it apart.
    const float m = (squads.morale[s] < 0.0f) ? 0.0f
                  : (squads.morale[s] > 1.0f) ? 1.0f : squads.morale[s];
    const float d = (squads.discipline[s] < 0.0f) ? 0.0f
                  : (squads.discipline[s] > 1.0f) ? 1.0f : squads.discipline[s];

    const float cohesion = d * m;
    float c = kMinCompression + (1.0f - kMinCompression) * cohesion;
    if (squads.contact[s]) c *= kContactCompression;
    return c;
}

Vec2 slotWorldPosition(const SquadHot& squads, size_t s,
                       uint16_t slotIndex, uint32_t memberCount) {
    const FormationShape shape = (FormationShape)squads.shape[s];
    const Vec2 raw = formationSlot(shape, slotIndex, memberCount);
    // updateSquadAggregate sets centroid to the mean of member positions, but
    // formationSlot's local origin is the formation's front-center, not its
    // mean (rank 0 sits at forward = 0, every later rank is negative). The
    // two points differ unless we recenter here: subtract the formation's
    // own mean offset so the mean of a squad's slot positions is exactly the
    // centroid, making the centroid a genuine fixed point instead of one the
    // squad chases backward every tick.
    const Vec2 mean = formationMeanOffset(shape, memberCount);
    // Compression scales the MEAN-CENTERED depth, not the raw depth. Scaling
    // the raw value would move the mean of the slot offsets off zero, and
    // formationMeanOffset exists precisely to keep it there: without that, the
    // squad chases its own receding anchor backward every tick.
    const float compression = squadCompression(squads, s);
    const Vec2 local{ raw.x - mean.x, (raw.y - mean.y) * compression };
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    // Local +y is "toward the enemy" and maps onto facing; local +x is
    // squad-right, which is facing rotated 90 degrees clockwise.
    const float rightX =  fy;
    const float rightY = -fx;

    // While advancing, lead the whole formation slightly ahead of where it
    // stands, along the squad's MOVEMENT direction (moveX/Y), not its facing.
    // Facing still orients the formation (rightX/rightY above) and points at
    // the enemy; the lead is a separate world-space offset so a squad can
    // side-step into clear terrain while still facing the foe (design §7.2).
    // Holding squads get no lead, so the centroid stays a fixed point exactly
    // as formationMeanOffset arranged.
    // Engaged squads do not lead: a formation that has met the enemy is
    // holding ground, not marching. Suppressed here rather than in the caller
    // so "where is this slot" stays answerable from this one function.
    const float lead = (squads.order[s] == (uint8_t)SquadOrder::Advance
                        && !squads.contact[s])
                     ? kAdvanceLead : 0.0f;

    const float leadX = (lead != 0.0f) ? squads.moveX[s] : 0.0f;
    const float leadY = (lead != 0.0f) ? squads.moveY[s] : 0.0f;

    // Built from the ANCHOR, not the centroid (design 5.2). While the squad is
    // free the two are equal, so this is behaviour-preserving; while it is
    // engaged the anchor is frozen, which is what stops the formation chasing
    // its own drifting mean.
    return Vec2{
        squads.anchorX[s] + local.x * rightX + local.y * fx + lead * leadX,
        squads.anchorY[s] + local.x * rightY + local.y * fy + lead * leadY
    };
}

void steerToSlot(SoldierHot& soldiers, const SquadHot& squads,
                 size_t i, float dt) {
    const uint16_t s = soldiers.squadId[i];
    steerToward(soldiers, i,
                slotWorldPosition(squads, s, soldiers.slotIndex[i],
                                  squads.memberCount[s]),
                dt);
}

void steerToward(SoldierHot& soldiers, size_t i, Vec2 target, float dt,
                 float speedScale) {
    const float dx = target.x - soldiers.posX[i];
    const float dy = target.y - soldiers.posY[i];
    const float distSq = dx * dx + dy * dy;

    // Three scales multiply here and each is owned by the layer that knows
    // about it: the unit type's base speed, what the man is wearing, and
    // whatever the caller passed (flight, and from Task 7 the formation).
    const Loadout& lo = loadoutOf(soldiers.troopClass[i]);
    const float speed = kUnitStats[(int)soldiers.unitType[i]].speed
                      * kArmorSpeedScale[(int)lo.armor] * speedScale;

    // A deadband stops soldiers vibrating on their slot. Without it, every
    // soldier in a stationary army jitters at full speed across the slot.
    constexpr float kArriveRadius = 2.0f;
    if (distSq < kArriveRadius * kArriveRadius) {
        soldiers.velX[i] = 0.0f;
        soldiers.velY[i] = 0.0f;
        return;
    }

    const float dist = std::sqrt(distSq);
    // Ease off over the last stride so arrival does not overshoot.
    const float approach = (dist < speed * dt * 4.0f)
                         ? dist / (speed * dt * 4.0f) : 1.0f;
    soldiers.velX[i] = (dx / dist) * speed * approach;
    soldiers.velY[i] = (dy / dist) * speed * approach;
}
