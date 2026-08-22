#include <doctest/doctest.h>
#include "Formation.hpp"
#include "Soldiers.hpp"
#include "Squads.hpp"
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

TEST_CASE("a wedge tapers while a line does not") {
    // "Measurably different geometry from Line for the same member count":
    // a Line's front rank (rank 0) already spans the formation's full
    // width, while a Wedge's front rank is a single point (the tip) that
    // only widens going back. Checking width at slot 0 alone discriminates
    // the two shapes, and would fail if shapeForUnit ever mapped Cavalry to
    // Line instead of Wedge.
    const uint32_t n = 25;
    const Vec2 lineFront = formationSlot(FormationShape::Line, 0, n);
    const Vec2 wedgeTip   = formationSlot(FormationShape::Wedge, 0, n);

    CHECK(std::abs(lineFront.x) > 0.0f);
    CHECK(wedgeTip.x == doctest::Approx(0.0f));
}

TEST_CASE("an empty squad returns the origin rather than dividing by zero") {
    const Vec2 s = formationSlot(FormationShape::Line, 0, 0);
    CHECK(s.x == doctest::Approx(0.0f));
    CHECK(s.y == doctest::Approx(0.0f));
}

TEST_CASE("rankOfSlot agrees with the forward offset formationSlot produces") {
    // The rank a slot belongs to is observable from formationSlot's output:
    // rank r sits at forward = -r * spacing. Deriving the expected value from
    // the function under test's own sibling is what makes this a consistency
    // check rather than a restatement of the implementation.
    struct Case { FormationShape shape; float spacing; };
    const Case cases[] = {
        { FormationShape::Line,   kSlotSpacing },
        { FormationShape::Column, kSlotSpacing },
        { FormationShape::Wedge,  kSlotSpacing },
        { FormationShape::Loose,  kSlotSpacing * 2.0f },
    };

    for (const Case& c : cases) {
        for (uint32_t n : { 1u, 2u, 7u, 25u, 60u }) {
            for (uint16_t i = 0; i < (uint16_t)n; ++i) {
                const Vec2 s = formationSlot(c.shape, i, n);
                const uint32_t expected = (uint32_t)(-s.y / c.spacing + 0.5f);
                CHECK(rankOfSlot(c.shape, i, n) == expected);
            }
        }
    }
}

TEST_CASE("rankOfSlot puts slot 0 in the front rank for every shape") {
    for (FormationShape shape : { FormationShape::Line, FormationShape::Column,
                                  FormationShape::Wedge, FormationShape::Loose }) {
        CHECK(rankOfSlot(shape, 0, 40) == 0u);
    }
}

TEST_CASE("rankOfSlot handles an empty squad without dividing by zero") {
    CHECK(rankOfSlot(FormationShape::Line, 0, 0) == 0u);
}

TEST_CASE("a fully cohesive squad is not compressed at all") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 1.0f;
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;
    CHECK(squadCompression(q, 0) == doctest::Approx(1.0f));
}

TEST_CASE("a broken squad compresses to the floor, never past it") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 0.0f;
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;
    CHECK(squadCompression(q, 0) == doctest::Approx(kMinCompression));
    CHECK(squadCompression(q, 0) > 0.0f);
}

TEST_CASE("compression is monotone in cohesion") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;

    float previous = -1.0f;
    for (float m : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        q.morale[0] = m;
        const float c = squadCompression(q, 0);
        CHECK(c > previous);
        previous = c;
    }
}

TEST_CASE("an engaged squad presses tighter than a free one at equal morale") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 1.0f;
    q.discipline[0] = 1.0f;

    q.contact[0] = 0;
    const float freeC = squadCompression(q, 0);
    q.contact[0] = 1;
    const float engagedC = squadCompression(q, 0);
    CHECK(engagedC < freeC);
}

TEST_CASE("compression preserves rank ORDER, only rank spacing") {
    // The failure this guards against is a shaken squad collapsing into a
    // point, which would make rear ranks fight and break the front-rank-only
    // property entirely.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.contact[0] = 1;
    q.morale[0] = 0.0f;               // worst case: floor times contact squeeze
    q.facingX[0] = 0.0f;
    q.facingY[0] = 1.0f;
    q.anchorX[0] = 0.0f;
    q.anchorY[0] = 0.0f;
    q.memberCount[0] = 40;
    q.order[0] = (uint8_t)SquadOrder::Hold;

    const uint32_t width = detail::rankWidth(40, 2.0f);
    float previousForward = 1e30f;
    for (uint32_t rank = 0; rank * width < 40; ++rank) {
        const uint16_t slot = (uint16_t)(rank * width);
        const Vec2 p = slotWorldPosition(q, 0, slot, 40);
        const float forward = p.x * q.facingX[0] + p.y * q.facingY[0];
        CHECK(forward < previousForward);
        previousForward = forward;
    }
}

TEST_CASE("compression keeps the mean slot offset at the anchor") {
    // formationMeanOffset exists so the anchor is a genuine fixed point.
    // Scaling the RAW forward offset instead of the mean-centered one would
    // break that and make the formation drift backward every tick.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.morale[0] = 0.3f;               // some arbitrary partial compression
    q.contact[0] = 0;
    q.facingX[0] = 1.0f;
    q.facingY[0] = 0.0f;
    q.anchorX[0] = 500.0f;
    q.anchorY[0] = 300.0f;
    q.memberCount[0] = 37;
    q.order[0] = (uint8_t)SquadOrder::Hold;

    float sumX = 0.0f, sumY = 0.0f;
    for (uint16_t k = 0; k < 37; ++k) {
        const Vec2 p = slotWorldPosition(q, 0, k, 37);
        sumX += p.x;
        sumY += p.y;
    }
    CHECK(sumX / 37.0f == doctest::Approx(500.0f).epsilon(1e-4));
    CHECK(sumY / 37.0f == doctest::Approx(300.0f).epsilon(1e-4));
}
