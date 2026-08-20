#include <doctest/doctest.h>
#include "Squads.hpp"
#include "Simulation.hpp"
#include <vector>

namespace {
// Builds a soldier array with a fixed squad assignment, bypassing Simulation
// so the membership rebuild can be tested on its own.
SoldierHot makeSoldiers(const std::vector<uint16_t>& squadIds,
                        const std::vector<uint16_t>& slotIndices) {
    SoldierHot s;
    for (size_t i = 0; i < squadIds.size(); ++i) {
        s.spawn(0.0f, 0.0f, 0.0f, 0.0f, Team::A, UnitType::Infantry, squadIds[i]);
        s.slotIndex[i] = slotIndices[i];
    }
    return s;
}

SquadHot makeSquads(uint32_t n) {
    SquadHot q;
    for (uint32_t i = 0; i < n; ++i) {
        q.spawn(Team::A, UnitType::Infantry);
    }
    return q;
}
} // namespace

TEST_CASE("members are grouped by squad") {
    SoldierHot s = makeSoldiers({1, 0, 1, 0}, {0, 0, 1, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 2);
    CHECK(q.memberCount[1] == 2);
    for (uint32_t i = 0; i < q.memberCount[0]; ++i) {
        CHECK(s.squadId[members[q.memberStart[0] + i]] == 0);
    }
    for (uint32_t i = 0; i < q.memberCount[1]; ++i) {
        CHECK(s.squadId[members[q.memberStart[1] + i]] == 1);
    }
}

TEST_CASE("within-squad order follows previous slotIndex, not array position") {
    // Soldier 0 sits earlier in the array but held slot 2; soldier 2 held
    // slot 0. Ordering by array position would put soldier 0 first, which is
    // exactly the instability that makes an officer's successor arbitrary.
    SoldierHot s = makeSoldiers({0, 0, 0}, {2, 1, 0});
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(members[0] == 2);
    CHECK(members[1] == 1);
    CHECK(members[2] == 0);
}

TEST_CASE("slotIndex is reassigned densely from zero") {
    SoldierHot s = makeSoldiers({0, 0, 0}, {5, 9, 2});
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(s.slotIndex[members[0]] == 0);
    CHECK(s.slotIndex[members[1]] == 1);
    CHECK(s.slotIndex[members[2]] == 2);
}

TEST_CASE("officer succession goes to the previously adjacent soldier") {
    // Slots 0,1,2 held by soldiers 7,3,5. Remove the officer (soldier 7) and
    // the next slot holder must be soldier 3, not whichever survivor happens
    // to sit first in the array.
    SoldierHot s = makeSoldiers({0, 0, 0}, {1, 2, 0});
    //                index:     0  1  2
    //                slot:      1  2  0   -> order is 2, 0, 1
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);
    REQUIRE(members[0] == 2);  // soldier 2 is the officer

    // Kill the officer and rebuild.
    s.state[2] = SoldierState::Dead;
    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 2);
    CHECK(members[0] == 0);  // soldier 0 held slot 1, so it inherits slot 0
    CHECK(s.slotIndex[0] == 0);
}

TEST_CASE("an empty squad has a zero-length range") {
    SoldierHot s = makeSoldiers({1, 1}, {0, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 0);
    CHECK(q.memberCount[1] == 2);
}

TEST_CASE("rebuilding twice is idempotent") {
    SoldierHot s = makeSoldiers({0, 1, 0, 1}, {0, 0, 1, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> first, second;

    rebuildSquadMembers(s, q, first);
    rebuildSquadMembers(s, q, second);

    CHECK(first == second);
}
