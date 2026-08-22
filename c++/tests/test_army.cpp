#include <doctest/doctest.h>
#include "Army.hpp"
#include "Squads.hpp"
#include "Units.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace {
// Two armies with squads placed by hand, so aggregates can be checked against
// values computed on paper rather than against whatever the sim happens to do.
struct ArmyFixture {
    SquadHot squads;
    ArmyHot armies;

    ArmyFixture() { armies.spawn(); armies.spawn(); }

    uint16_t add(Team t, UnitType u, float cx, float cy, uint32_t members) {
        const uint16_t s = (uint16_t)squads.count;
        squads.spawn(t, u);
        squads.centroidX[s] = cx;
        squads.centroidY[s] = cy;
        squads.memberCount[s] = members;
        return s;
    }
};
} // namespace

TEST_CASE("army strength sums its live squads by unit type") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 100.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 140.0f, 100.0f, 10);
    f.add(Team::A, UnitType::Archer,   100.0f,  60.0f, 12);
    f.add(Team::B, UnitType::Cavalry,  600.0f, 100.0f,  8);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.strengthInfantry[0] == doctest::Approx(30.0f));
    CHECK(f.armies.strengthArcher[0]   == doctest::Approx(12.0f));
    CHECK(f.armies.strengthCavalry[0]  == doctest::Approx(0.0f));
    CHECK(f.armies.strengthCavalry[1]  == doctest::Approx(8.0f));
}

TEST_CASE("a wiped-out squad contributes nothing") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 100.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 140.0f, 100.0f, 0);   // annihilated
    updateArmyAggregate(f.squads, f.armies);
    CHECK(f.armies.strengthInfantry[0] == doctest::Approx(20.0f));
}

TEST_CASE("the front line sits on the infantry, not on the whole army") {
    // Archers stand well behind the line. If the front were the mean of every
    // squad it would be dragged backward and 'behind our line' would stop
    // meaning anything useful.
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 200.0f, 140.0f, 20);
    f.add(Team::A, UnitType::Archer,    50.0f, 120.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 120.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontX[0] == doctest::Approx(200.0f));
    CHECK(f.armies.frontY[0] == doctest::Approx(120.0f));
}

TEST_CASE("the front direction points at the enemy army and is unit length") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontDirX[0] == doctest::Approx(1.0f));
    CHECK(f.armies.frontDirY[0] == doctest::Approx(0.0f));
    CHECK(f.armies.frontDirX[1] == doctest::Approx(-1.0f));

    const float len = std::sqrt(f.armies.frontDirX[0] * f.armies.frontDirX[0]
                              + f.armies.frontDirY[0] * f.armies.frontDirY[0]);
    CHECK(len == doctest::Approx(1.0f));
}

TEST_CASE("an army with no infantry falls back to its whole-army centroid") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer, 300.0f, 200.0f, 10);
    f.add(Team::B, UnitType::Infantry, 800.0f, 200.0f, 10);
    updateArmyAggregate(f.squads, f.armies);
    CHECK(f.armies.frontX[0] == doctest::Approx(300.0f));
}

TEST_CASE("an annihilated army produces no NaN") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 0);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontX[0] == f.armies.frontX[0]);            // not NaN
    const float len = std::sqrt(f.armies.frontDirX[0] * f.armies.frontDirX[0]
                              + f.armies.frontDirY[0] * f.armies.frontDirY[0]);
    CHECK(len == doctest::Approx(1.0f));
}

TEST_CASE("squadStrength weights members by unit type") {
    ArmyFixture f;
    const uint16_t inf = f.add(Team::A, UnitType::Infantry, 0.0f, 0.0f, 10);
    const uint16_t cav = f.add(Team::A, UnitType::Cavalry,  0.0f, 0.0f, 10);
    // Cavalry hit harder per man, so an equal head count is not equal strength.
    CHECK(squadStrength(f.squads, cav) > squadStrength(f.squads, inf));
}
