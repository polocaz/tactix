#include <doctest/doctest.h>
#include "Units.hpp"
#include "Formation.hpp"

TEST_CASE("unit stats table is indexed by UnitType") {
    CHECK(kUnitStats[(int)UnitType::Infantry].speed == doctest::Approx(45.0f));
    CHECK(kUnitStats[(int)UnitType::Archer].speed   == doctest::Approx(42.0f));
    CHECK(kUnitStats[(int)UnitType::Cavalry].speed  == doctest::Approx(95.0f));
}

TEST_CASE("only archers have a ranged attack") {
    CHECK(kUnitStats[(int)UnitType::Archer].range > 0.0f);
    CHECK(kUnitStats[(int)UnitType::Infantry].range == doctest::Approx(0.0f));
    CHECK(kUnitStats[(int)UnitType::Cavalry].range  == doctest::Approx(0.0f));
}

TEST_CASE("archer range exceeds the soldier perception radius") {
    // This gap is the entire justification for the squad tier: an archer
    // cannot perceive its own best target, so the squad must assign one.
    CHECK(kUnitStats[(int)UnitType::Archer].range > kSeekRadius);
}

TEST_CASE("shapeForUnit maps each unit type to its own formation") {
    // Nothing else exercises this dispatch. Formation geometry is tested per
    // shape, and roster composition is tested per unit type, but until this
    // test the mapping that joins them was covered only by the committed state
    // digest. That would tell you something moved, never that Loose and Wedge
    // had quietly collapsed into Line.
    CHECK(shapeForUnit(UnitType::Infantry) == FormationShape::Line);
    CHECK(shapeForUnit(UnitType::Archer)   == FormationShape::Loose);
    CHECK(shapeForUnit(UnitType::Cavalry)  == FormationShape::Wedge);
}

TEST_CASE("the shape a unit gets changes its footprint") {
    // The mapping above only matters because the shapes differ in the field.
    // Archers deploy Loose specifically so a volley cannot wipe the unit, so an
    // archer squad must occupy more ground than an infantry squad of the same
    // size. This fails if shapeForUnit is flattened even if the enum survives.
    const uint32_t members = 25;
    const Vec2 infantry = formationExtent(shapeForUnit(UnitType::Infantry), members);
    const Vec2 archers  = formationExtent(shapeForUnit(UnitType::Archer),   members);
    CHECK(archers.x > infantry.x);

    // Cavalry form a wedge, which tapers to a single soldier at the tip rather
    // than presenting a flat rank, so its front is narrower than a line's.
    const Vec2 cavalry = formationExtent(shapeForUnit(UnitType::Cavalry), members);
    CHECK(cavalry.x != infantry.x);
}
