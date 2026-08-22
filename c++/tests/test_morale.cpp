#include <doctest/doctest.h>
#include "Morale.hpp"
#include "Squads.hpp"
#include "Units.hpp"
#include <vector>

namespace {
// One squad, fully fresh, with the given size and discipline.
SquadHot oneSquad(uint32_t members, float discipline, UnitType u = UnitType::Infantry) {
    SquadHot q;
    q.spawn(Team::A, u);
    q.memberCount[0] = members;
    q.morale[0] = 1.0f;
    q.discipline[0] = discipline;
    return q;
}
} // namespace

TEST_CASE("an untouched squad recovers toward full morale and stops at 1") {
    SquadHot q = oneSquad(20, kDisciplineInfantry);
    q.morale[0] = 0.5f;
    const std::vector<uint32_t> none(1, 0u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(q, none, noOfficer, 1.0f);
    CHECK(q.morale[0] > 0.5f);

    for (int t = 0; t < 100; ++t) updateMorale(q, none, noOfficer, 1.0f);
    CHECK(q.morale[0] == doctest::Approx(1.0f));
}

TEST_CASE("casualties drop morale in proportion to the fraction lost") {
    SquadHot small = oneSquad(10, 1.0f);
    SquadHot large = oneSquad(100, 1.0f);
    const std::vector<uint32_t> twoDead(1, 2u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(small, twoDead, noOfficer, 1.0f / 60.0f);
    updateMorale(large, twoDead, noOfficer, 1.0f / 60.0f);

    // Two dead out of ten hurts far more than two out of a hundred.
    CHECK(small.morale[0] < large.morale[0]);
}

TEST_CASE("losing the officer costs morale on top of the casualty itself") {
    SquadHot withOfficer = oneSquad(20, 1.0f);
    SquadHot without     = oneSquad(20, 1.0f);
    const std::vector<uint32_t> oneDead(1, 1u);

    updateMorale(withOfficer, oneDead, std::vector<uint8_t>(1, (uint8_t)1), 1.0f / 60.0f);
    updateMorale(without,     oneDead, std::vector<uint8_t>(1, (uint8_t)0), 1.0f / 60.0f);

    CHECK(withOfficer.morale[0] < without.morale[0]);
}

TEST_CASE("discipline blunts losses and speeds recovery") {
    SquadHot steady = oneSquad(20, 0.95f);
    SquadHot levy   = oneSquad(20, 0.20f);
    const std::vector<uint32_t> fourDead(1, 4u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(steady, fourDead, noOfficer, 1.0f / 60.0f);
    updateMorale(levy,   fourDead, noOfficer, 1.0f / 60.0f);
    CHECK(steady.morale[0] > levy.morale[0]);
}

TEST_CASE("an enemy in the rear arc erodes morale over time") {
    SquadHot flanked = oneSquad(20, 1.0f);
    SquadHot safe    = oneSquad(20, 1.0f);
    flanked.morale[0] = 0.5f;
    safe.morale[0]    = 0.5f;
    flanked.rearThreat[0] = 1;
    const std::vector<uint32_t> none(1, 0u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    for (int t = 0; t < 60; ++t) {
        updateMorale(flanked, none, noOfficer, 1.0f / 60.0f);
        updateMorale(safe,    none, noOfficer, 1.0f / 60.0f);
    }
    CHECK(flanked.morale[0] < safe.morale[0]);
}

TEST_CASE("morale is clamped to 0..1 under absurd input") {
    SquadHot q = oneSquad(4, 0.0f);
    const std::vector<uint32_t> wipe(1, 400u);
    updateMorale(q, wipe, std::vector<uint8_t>(1, (uint8_t)1), 1.0f);
    CHECK(q.morale[0] >= 0.0f);
    CHECK(q.morale[0] <= 1.0f);
}

TEST_CASE("an empty squad's morale is left alone rather than divided by zero") {
    SquadHot q = oneSquad(0, 1.0f);
    q.morale[0] = 0.7f;
    updateMorale(q, std::vector<uint32_t>(1, 0u),
                 std::vector<uint8_t>(1, (uint8_t)0), 1.0f / 60.0f);
    CHECK(q.morale[0] == doctest::Approx(0.7f));
}

TEST_CASE("a squad below its rout threshold breaks") {
    SquadHot q = oneSquad(20, 0.0f);       // no discipline: threshold is the base
    q.morale[0] = kBaseRoutThreshold - 0.01f;
    applyRoutTransitions(q, 1.0f / 60.0f);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);
}

TEST_CASE("a disciplined squad holds where an undisciplined one breaks") {
    SquadHot steady = oneSquad(20, 1.0f);
    SquadHot levy   = oneSquad(20, 0.0f);
    const float m = kBaseRoutThreshold - 0.01f;
    steady.morale[0] = m;
    levy.morale[0]   = m;

    applyRoutTransitions(steady, 1.0f / 60.0f);
    applyRoutTransitions(levy,   1.0f / 60.0f);

    CHECK(steady.order[0] != (uint8_t)SquadOrder::Rout);
    CHECK(levy.order[0]   == (uint8_t)SquadOrder::Rout);
}

TEST_CASE("a routing squad rallies only after sustained safety") {
    SquadHot q = oneSquad(20, 0.0f);
    q.order[0] = (uint8_t)SquadOrder::Rout;
    q.morale[0] = kRallyThreshold + 0.05f;
    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;    // clear of enemies

    const float dt = 1.0f / 60.0f;
    const int needed = (int)(kRallyDuration / dt);
    for (int t = 0; t < needed - 2; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);   // not yet

    for (int t = 0; t < 4; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Hold);
}

TEST_CASE("an enemy nearby resets the rally timer") {
    SquadHot q = oneSquad(20, 0.0f);
    q.order[0] = (uint8_t)SquadOrder::Rout;
    q.morale[0] = kRallyThreshold + 0.05f;

    const float dt = 1.0f / 60.0f;
    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;
    for (int t = 0; t < (int)(kRallyDuration / dt) - 10; ++t) applyRoutTransitions(q, dt);

    q.nearestEnemyDist[0] = kRallyRadius * 0.5f;      // an enemy closes in
    applyRoutTransitions(q, dt);
    CHECK(q.rallyTimer[0] == doctest::Approx(0.0f));

    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;
    for (int t = 0; t < 20; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);   // had to start over
}

TEST_CASE("rout clears contact so a broken formation stops holding a line") {
    SquadHot q = oneSquad(20, 0.0f);
    q.morale[0] = 0.0f;
    q.contact[0] = 1;
    applyRoutTransitions(q, 1.0f / 60.0f);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);
    CHECK(q.contact[0] == 0);
}

TEST_CASE("disciplineForUnit gives every type a distinct steadiness") {
    CHECK(disciplineForUnit(UnitType::Infantry) == doctest::Approx(kDisciplineInfantry));
    CHECK(disciplineForUnit(UnitType::Archer)   == doctest::Approx(kDisciplineArcher));
    CHECK(disciplineForUnit(UnitType::Cavalry)  == doctest::Approx(kDisciplineCavalry));
}
