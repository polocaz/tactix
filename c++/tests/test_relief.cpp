#include <doctest/doctest.h>
#include "Army.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include <cmath>

namespace {
// A spent front-line maniple with a fresh reserve directly behind it. Team A
// faces +x, so "behind" is -x.
SquadHot twoManiples() {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);   // 0: the spent front line
    q.spawn(Team::A, UnitType::Infantry);   // 1: the reserve
    for (size_t s = 0; s < 2; ++s) {
        q.troopClass[s] = (uint8_t)TroopClass::Legionary;
        q.shape[s] = (uint8_t)FormationShape::Manipular;
        q.facingX[s] = 1.0f;
        q.facingY[s] = 0.0f;
        q.initialMemberCount[s] = 40;
        q.memberCount[s] = 40;
        q.morale[s] = 1.0f;
    }
    q.centroidX[0] = 500.0f; q.centroidY[0] = 400.0f;
    q.centroidX[1] = 400.0f; q.centroidY[1] = 400.0f;   // behind the front line
    q.role[0] = (uint8_t)SquadRole::Line;
    q.role[1] = (uint8_t)SquadRole::Reserve;
    q.contact[0] = 1;
    q.memberCount[0] = 20;   // half gone, well under kReliefLossFraction
    return q;
}

ArmyHot oneArmy() {
    ArmyHot a;
    a.spawn();
    a.spawn();
    a.frontDirX[0] = 1.0f;
    a.frontDirY[0] = 0.0f;
    return a;
}
} // namespace

TEST_CASE("a spent maniple is paired with the reserve behind it") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);

    CHECK(q.reliefStage[0] == kReliefRetiring);
    CHECK(q.reliefStage[1] == kReliefAdvancing);
    CHECK(q.reliefPartner[0] == 1);
    CHECK(q.reliefPartner[1] == 0);
    CHECK(q.role[1] == (uint8_t)SquadRole::Line);
}

TEST_CASE("a fresh maniple is not relieved") {
    SquadHot q = twoManiples();
    q.memberCount[0] = 40;      // untouched
    q.morale[0] = 1.0f;
    q.contactDuration[0] = 0.0f;
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
}

TEST_CASE("a maniple out of contact is not relieved") {
    // Relief is for a line that is being ground down, not for one standing idle.
    SquadHot q = twoManiples();
    q.contact[0] = 0;
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
}

TEST_CASE("a shaken maniple is relieved before it routs") {
    SquadHot q = twoManiples();
    q.memberCount[0] = 40;                       // not bled
    q.morale[0] = kReliefMoraleThreshold - 0.01f; // but shaken
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefRetiring);
    // The whole point of the threshold ordering: this squad is still above the
    // morale at which it would break, so the relief gets there first.
    CHECK(q.morale[0] > kRallyThreshold);
}

TEST_CASE("a tired maniple is relieved even at full strength and morale") {
    SquadHot q = twoManiples();
    q.memberCount[0] = 40;
    q.morale[0] = 1.0f;
    q.contactDuration[0] = kReliefContactSeconds + 1.0f;
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefRetiring);
}

TEST_CASE("a reserve standing in FRONT of the line is not used") {
    // "Behind" is a dot product against the army front direction, and a reserve
    // that is somehow ahead of the line cannot be retired through.
    SquadHot q = twoManiples();
    q.centroidX[1] = 600.0f;   // in front of squad 0, not behind it
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
}

TEST_CASE("a reserve too far back is not used") {
    SquadHot q = twoManiples();
    q.centroidX[1] = 500.0f - kReliefSearchRadius * 2.0f;
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
}

TEST_CASE("relief completes once the two maniples have separated") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    REQUIRE(q.reliefStage[0] == kReliefRetiring);

    // Still overlapping: the swap is not finished.
    q.centroidX[0] = 500.0f;
    q.centroidX[1] = 500.0f - kReliefClearDistance * 0.5f;
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefRetiring);

    // Separated: the swap completes and the two exchange roles.
    q.centroidX[0] = 400.0f;
    q.centroidX[1] = 520.0f;
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
    CHECK(q.reliefStage[1] == kReliefIdle);
    CHECK(q.role[0] == (uint8_t)SquadRole::Reserve);
    CHECK(q.role[1] == (uint8_t)SquadRole::Line);
    CHECK(q.reliefPartner[0] == UINT16_MAX);
}

TEST_CASE("relief does not fire again inside the cooldown") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);

    // Complete the swap, then put the newly-retired squad back on the line and
    // make it spent again. The cooldown must refuse it.
    q.centroidX[0] = 400.0f;
    q.centroidX[1] = 520.0f;
    updateLineRelief(q, a, Team::A);
    REQUIRE(q.reliefStage[0] == kReliefIdle);

    q.role[0] = (uint8_t)SquadRole::Line;
    q.role[1] = (uint8_t)SquadRole::Reserve;
    q.contact[0] = 1;
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
}

TEST_CASE("a relief whose partner is annihilated releases rather than hanging") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    REQUIRE(q.reliefStage[0] == kReliefRetiring);

    q.memberCount[1] = 0;
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == kReliefIdle);
    CHECK(q.reliefPartner[0] == UINT16_MAX);
}

TEST_CASE("a retiring maniple is ordered to withdraw even though it is in contact") {
    // The contact halt normally overrides everything. It must not override
    // this, or the maniple stands and dies exactly where the relief was meant
    // to save it.
    SquadHot q = twoManiples();
    q.spawn(Team::B, UnitType::Infantry);   // 2: something to fight
    q.memberCount[2] = 20;
    q.centroidX[2] = 560.0f; q.centroidY[2] = 400.0f;
    q.targetSquad[0] = 2;

    ArmyHot a = oneArmy();
    updateArmyAggregate(q, a);
    updateLineRelief(q, a, Team::A);
    REQUIRE(q.reliefStage[0] == kReliefRetiring);

    TerrainField terrain;
    squadDecide(q, a, 0, terrain, kFixedTimestep);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Withdraw);
}

TEST_CASE("an advancing maniple aims beside the line rather than at it") {
    SquadHot q = twoManiples();
    q.spawn(Team::B, UnitType::Infantry);
    q.memberCount[2] = 20;
    q.centroidX[2] = 700.0f; q.centroidY[2] = 400.0f;
    q.targetSquad[1] = 2;

    ArmyHot a = oneArmy();
    updateArmyAggregate(q, a);
    updateLineRelief(q, a, Team::A);
    REQUIRE(q.reliefStage[1] == kReliefAdvancing);

    const Vec2 advancing = roleAnchorFor(q, a, 1);
    q.reliefStage[1] = kReliefIdle;
    const Vec2 ordinary = roleAnchorFor(q, a, 1);

    // The two squads are lined up on y == 400 and the enemy is due east, so the
    // offset must show up across the line rather than along it.
    CHECK(std::abs(advancing.y - ordinary.y) == doctest::Approx(kReliefLateralOffset));
}

TEST_CASE("a relief actually happens in a real battle") {
    // The unit tests above all construct the exact situation relief needs. This
    // one asserts the situation ARISES: every one of those tests can pass while
    // the feature is inert on a real field, which is the failure mode that
    // matters most for a rule the army tier only evaluates every 30 ticks.
    Simulation sim(2400, 1600, 42u, 0u);
    sim.init(1200);
    sim.setPaused(false);

    bool sawRelief = false;
    for (uint32_t t = 0; t < 6000 && !sawRelief; ++t) {
        sim.tick(kFixedTimestep);
        for (size_t s = 0; s < sim.getSquadCount(); ++s) {
            if (sim.squadReliefStage(s) != kReliefIdle) { sawRelief = true; break; }
        }
    }
    CHECK(sawRelief);
}
