#include <doctest/doctest.h>
#include "Formation.hpp"
#include <algorithm>
#include <cmath>

TEST_CASE("slot 0 sits on the front rank") {
    const Vec2 s = formationSlot(FormationShape::Line, 0, 25);
    CHECK(s.y == doctest::Approx(0.0f));
}

TEST_CASE("a line is wider than it is deep") {
    // Sample every slot and compare extents.
    float maxAbsRight = 0.0f, maxAbsForward = 0.0f;
    for (uint16_t i = 0; i < 25; ++i) {
        const Vec2 s = formationSlot(FormationShape::Line, i, 25);
        maxAbsRight   = std::max(maxAbsRight,   std::abs(s.x));
        maxAbsForward = std::max(maxAbsForward, std::abs(s.y));
    }
    CHECK(maxAbsRight > maxAbsForward);
}

TEST_CASE("a column is deeper than it is wide") {
    float maxAbsRight = 0.0f, maxAbsForward = 0.0f;
    for (uint16_t i = 0; i < 25; ++i) {
        const Vec2 s = formationSlot(FormationShape::Column, i, 25);
        maxAbsRight   = std::max(maxAbsRight,   std::abs(s.x));
        maxAbsForward = std::max(maxAbsForward, std::abs(s.y));
    }
    CHECK(maxAbsForward > maxAbsRight);
}

TEST_CASE("loose spacing is wider than line spacing for the same count") {
    const Vec2 line  = formationSlot(FormationShape::Line,  24, 25);
    const Vec2 loose = formationSlot(FormationShape::Loose, 24, 25);
    CHECK(std::abs(loose.x) > std::abs(line.x));
}

TEST_CASE("a wedge widens by two per rank") {
    // Rank r starts at slot r*r and holds 2r+1 slots, so slot 0 is the tip,
    // slots 1..3 are the second rank, slots 4..8 the third.
    CHECK(formationSlot(FormationShape::Wedge, 0, 9).y == doctest::Approx(0.0f));
    const Vec2 rank1 = formationSlot(FormationShape::Wedge, 1, 9);
    const Vec2 rank2 = formationSlot(FormationShape::Wedge, 4, 9);
    CHECK(rank1.y < 0.0f);
    CHECK(rank2.y < rank1.y);
}

TEST_CASE("slots are stable across calls") {
    // Same inputs must always give the same output. Formation slots are
    // recomputed every tick, so instability would jitter every soldier.
    for (uint16_t i = 0; i < 40; ++i) {
        const Vec2 a = formationSlot(FormationShape::Line, i, 40);
        const Vec2 b = formationSlot(FormationShape::Line, i, 40);
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }
}

TEST_CASE("an empty squad returns the origin rather than dividing by zero") {
    const Vec2 s = formationSlot(FormationShape::Line, 0, 0);
    CHECK(s.x == doctest::Approx(0.0f));
    CHECK(s.y == doctest::Approx(0.0f));
}
