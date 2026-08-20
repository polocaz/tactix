#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "Formation.hpp"
#include <cmath>

Vec2 slotWorldPosition(const SquadHot& squads, size_t s,
                       uint16_t slotIndex, uint32_t memberCount) {
    const FormationShape shape = shapeForUnit(squads.unitType[s]);
    const Vec2 raw = formationSlot(shape, slotIndex, memberCount);
    // updateSquadAggregate sets centroid to the mean of member positions, but
    // formationSlot's local origin is the formation's front-center, not its
    // mean (rank 0 sits at forward = 0, every later rank is negative). The
    // two points differ unless we recenter here: subtract the formation's
    // own mean offset so the mean of a squad's slot positions is exactly the
    // centroid, making the centroid a genuine fixed point instead of one the
    // squad chases backward every tick.
    const Vec2 mean = formationMeanOffset(shape, memberCount);
    const Vec2 local{ raw.x - mean.x, raw.y - mean.y };
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    // Local +y is "toward the enemy" and maps onto facing; local +x is
    // squad-right, which is facing rotated 90 degrees clockwise.
    const float rightX =  fy;
    const float rightY = -fx;
    return Vec2{
        squads.centroidX[s] + local.x * rightX + local.y * fx,
        squads.centroidY[s] + local.x * rightY + local.y * fy
    };
}

void steerToSlot(SoldierHot& soldiers, const SquadHot& squads,
                 size_t i, float dt) {
    const uint16_t s = soldiers.squadId[i];
    const Vec2 target = slotWorldPosition(squads, s, soldiers.slotIndex[i],
                                          squads.memberCount[s]);

    const float dx = target.x - soldiers.posX[i];
    const float dy = target.y - soldiers.posY[i];
    const float distSq = dx * dx + dy * dy;

    const float speed = kUnitStats[(int)soldiers.unitType[i]].speed;

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
