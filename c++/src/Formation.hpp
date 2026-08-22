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

    float spacing = kSlotSpacing;
    float aspect  = 2.0f;
    if (shape == FormationShape::Column) {
        aspect = 0.5f;
    } else if (shape == FormationShape::Loose) {
        spacing = kSlotSpacing * 2.0f;
    }

    const uint32_t width = detail::rankWidth(memberCount, aspect);
    const uint32_t row = slotIndex / width;
    const uint32_t col = slotIndex % width;

    const float right = ((float)col - (float)(width - 1) * 0.5f) * spacing;
    const float forward = -(float)row * spacing;
    return Vec2{ right, forward };
}

// Which rank a slot belongs to, rank 0 being the front. This MUST mirror
// formationSlot's own layout: Wedge packs rank r into slots r*r .. r*r+2r, so
// its rank is floor(sqrt(i)), while the grid shapes rank by integer division
// on the same width rankWidth computes. Kept next to formationSlot precisely
// so a change to one is an obvious prompt to change the other. Contact
// detection (Contact.cpp) is the consumer: it tests only the front rank, and a
// wrong rank here would silently make a whole squad or none of it eligible.
inline uint32_t rankOfSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount) {
    if (memberCount == 0) return 0;

    if (shape == FormationShape::Wedge) {
        return (uint32_t)std::sqrt((float)slotIndex);
    }

    // Loose differs from Line only in spacing, not in aspect, so it shares
    // this branch. Column is the narrow-and-deep aspect.
    const float aspect = (shape == FormationShape::Column) ? 0.5f : 2.0f;
    const uint32_t width = detail::rankWidth(memberCount, aspect);
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
