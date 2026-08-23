#pragma once
#include "Units.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float kSlotSpacing = 12.0f;

// Separation (Simulation::phaseSoldierSteerChunk) exists to stop soldiers
// overlapping, not to enforce spacing -- formation shape does that. It MUST
// stay below kSlotSpacing: a soldier's rendered footprint is ~4px radius
// (Renderer.cpp's agentSize), so two soldiers visually overlap under ~8px
// apart, and correctly-slotted Line ranks sit exactly kSlotSpacing (12px)
// apart. 10px sits between those two bounds -- comfortably clear of visual
// overlap, comfortably clear of a held Line formation -- so a soldier
// standing on its slot feels zero separation force. Raising this back toward
// its old value (25px) would put it inside kSlotSpacing again, and
// separation would fight the formation instead of just preventing overlap:
// that is the bug this constant fixes. If a formation shape is ever added
// with tighter spacing than this, shrink this constant to match, not the
// other way around.
constexpr float kSeparationRadius = 10.0f;

// How far outside an obstacle a soldier is asked to stand. Two things MUST
// use the same number, for exactly the reason kSeparationRadius documents
// above: Simulation::clearOfObstacles slides a formation slot this far clear
// of a wall, and the obstacle-avoidance push in phaseSoldierSteerChunk fades
// to zero at this distance. If the avoidance reach were larger than the
// clearance, a soldier standing precisely on its cleared slot would still
// feel a push, get shoved off, walk back, and grind against the wall for the
// whole battle -- which is what a 50px avoidance reach against a 4px
// clearance actually did. Keep them equal.
constexpr float kObstacleStandoff = kSeparationRadius + kSoldierRadius;

constexpr uint32_t kFormationShapeCount = 9;

// Everything a formation IS, in one row. Geometry (spacing, aspect) and
// behavior (the rest) live together because they are the same decision: a
// shieldwall is tight AND slow AND covered, and splitting those across three
// files is how they drift apart.
//
// cover is in PERCENTAGE POINTS added to the shield block chance, per arc
// (front, side, rear). Signed because Mob subtracts.
struct FormationTraits {
    float   spacing;        // multiplies kSlotSpacing
    float   aspect;         // target width/depth for the grid shapes
    float   speed;          // multiplies the unit's base speed
    float   turn;           // multiplies kFacingSlewRate
    uint8_t fightingRanks;  // how many ranks may reach an enemy
    float   reach;          // multiplies kMeleeReach
    float   cooldown;       // multiplies kMeleeCooldown
    int8_t  cover[3];       // front, side, rear
};

// The first four rows reproduce the pre-table behavior exactly, so introducing
// this table is not a behavior change for any existing shape. Wedge's aspect is
// never read (its rank layout is floor(sqrt(i))) and is set to Line's value so
// the row is not a special case for anything but the two functions that already
// branch on it.
constexpr FormationTraits kFormationTraits[kFormationShapeCount] = {
    /* Line       */ { 1.00f, 2.0f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Column     */ { 1.00f, 0.5f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Wedge      */ { 1.00f, 2.0f, 1.00f, 1.20f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Loose      */ { 2.00f, 2.0f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Shieldwall */ { 0.75f, 3.0f, 0.60f, 0.50f, 1, 1.0f, 1.0f, { +30,  +5,   0 } },
    /* Phalanx    */ { 0.85f, 1.5f, 0.50f, 0.35f, 3, 1.6f, 1.0f, { +20,   0,   0 } },
    /* Testudo    */ { 0.60f, 1.2f, 0.30f, 0.40f, 1, 1.0f, 2.2f, { +55, +45, +35 } },
    /* Manipular  */ { 1.00f, 2.5f, 0.95f, 1.00f, 1, 1.0f, 1.0f, { +10,   0,   0 } },
    /* Mob        */ { 1.50f, 1.0f, 1.05f, 2.00f, 1, 1.0f, 1.2f, { -10, -10, -10 } },
};

constexpr const FormationTraits& traitsOf(FormationShape s) {
    return kFormationTraits[(uint32_t)s < kFormationShapeCount ? (uint32_t)s : 0u];
}

namespace detail {

// Smallest w such that w * ceil(n/w) >= n and w/depth is near the target
// aspect. Computed by search rather than closed form: n is at most a few
// hundred, and a loop is easier to verify than the algebra.
inline uint32_t rankWidth(uint32_t memberCount, float aspect) {
    if (memberCount <= 1) return 1;
    const float ideal = std::sqrt((float)memberCount * aspect);
    uint32_t w = (uint32_t)std::ceil(ideal);
    if (w < 1) w = 1;
    if (w > memberCount) w = memberCount;
    return w;
}

// The single definition of how wide a shape stands. formationSlot and
// rankOfSlot BOTH call this, which is what makes their agreement structural
// instead of a promise in a comment that a future edit has to remember.
inline uint32_t shapeWidth(FormationShape shape, uint32_t memberCount) {
    return rankWidth(memberCount, traitsOf(shape).aspect);
}

} // namespace detail

// Returns an offset in squad-local space: +x is squad-right, +y is toward
// the enemy. Rank 0 is the front, so all slots have y <= 0.
inline Vec2 formationSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount) {
    if (memberCount == 0) return Vec2{0.0f, 0.0f};

    if (shape == FormationShape::Wedge) {
        // Rank r begins at slot r*r and holds 2r+1 slots, so r = floor(sqrt(i)).
        const uint32_t r = (uint32_t)std::sqrt((float)slotIndex);
        const uint32_t posInRank = slotIndex - r * r;   // 0 .. 2r
        const float col = (float)posInRank - (float)r;  // -r .. +r
        return Vec2{ col * kSlotSpacing, -(float)r * kSlotSpacing };
    }

    const float spacing = kSlotSpacing * traitsOf(shape).spacing;

    const uint32_t width = detail::shapeWidth(shape, memberCount);
    const uint32_t row = slotIndex / width;
    const uint32_t col = slotIndex % width;

    const float right = ((float)col - (float)(width - 1) * 0.5f) * spacing;
    const float forward = -(float)row * spacing;
    return Vec2{ right, forward };
}

// Which rank a slot belongs to, rank 0 being the front. Wedge packs rank r into
// slots r*r .. r*r+2r, so its rank is floor(sqrt(i)); every grid shape ranks by
// integer division on the width detail::shapeWidth computes, which is the same
// call formationSlot makes.
//
// This used to be a hand-maintained mirror of formationSlot, asking a future
// edit to remember to change two switches together. It is now structural: both
// functions read one aspect value out of kFormationTraits through one helper.
// Contact detection (Contact.cpp) and melee fighting depth (Combat.cpp) are the
// consumers, and a wrong rank here would silently make a whole squad or none of
// it eligible to fight.
inline uint32_t rankOfSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount) {
    if (memberCount == 0) return 0;

    if (shape == FormationShape::Wedge) {
        return (uint32_t)std::sqrt((float)slotIndex);
    }

    // Both this and formationSlot derive their width from detail::shapeWidth,
    // which reads one aspect value out of kFormationTraits. That is what makes
    // the mirror structural: there is one definition of how wide a shape
    // stands, and a change to it moves both functions together.
    const uint32_t width = detail::shapeWidth(shape, memberCount);
    return (uint32_t)slotIndex / width;
}

// Mean of every slot offset for this shape and count. slotWorldPosition
// subtracts it so that the mean of a squad's slot positions is exactly the
// squad centroid. Without this, centroid (a mean of member positions) and
// the formation's front-anchored origin are different points, and the squad
// translates backward every tick chasing its own receding centroid.
inline Vec2 formationMeanOffset(FormationShape shape, uint32_t memberCount) {
    if (memberCount == 0) return Vec2{0.0f, 0.0f};

    // Averaged by direct summation rather than a closed form: memberCount is
    // a few dozen, so the loop's cost is negligible and its correctness is
    // obvious by inspection. Accumulated in ascending slot order so the
    // result is bit-reproducible regardless of caller or thread.
    float sumX = 0.0f, sumY = 0.0f;
    for (uint32_t i = 0; i < memberCount; ++i) {
        const Vec2 s = formationSlot(shape, (uint16_t)i, memberCount);
        sumX += s.x;
        sumY += s.y;
    }
    return Vec2{ sumX / (float)memberCount, sumY / (float)memberCount };
}

// Bounding extent of a squad's own formation, in squad-local axes: x is the
// squad's width (right/left spread), y is its depth (front/back spread).
// Deployment (Simulation::init) uses this to size the grid it lays squads
// out on, so neighbouring squads' own footprints do not overlap.
//
// Scanned directly from formationSlot rather than derived in closed form:
// memberCount is a few dozen, so the loop cost is negligible and its
// correctness is obvious by inspection, matching formationMeanOffset above.
inline Vec2 formationExtent(FormationShape shape, uint32_t memberCount) {
    if (memberCount == 0) return Vec2{0.0f, 0.0f};

    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    for (uint32_t i = 0; i < memberCount; ++i) {
        const Vec2 s = formationSlot(shape, (uint16_t)i, memberCount);
        minX = std::min(minX, s.x);
        maxX = std::max(maxX, s.x);
        minY = std::min(minY, s.y);
        maxY = std::max(maxY, s.y);
    }
    return Vec2{ maxX - minX, maxY - minY };
}
