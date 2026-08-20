#include <doctest/doctest.h>
#include "Units.hpp"

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
