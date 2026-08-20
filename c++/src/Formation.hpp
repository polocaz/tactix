#pragma once
#include "Units.hpp"
#include <cmath>
#include <cstdint>

constexpr float kSlotSpacing = 12.0f;

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
