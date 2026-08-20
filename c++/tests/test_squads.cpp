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
    // Soldier 0 holds slot 2, soldier 1 holds slot 0 (the officer), soldier 2
    // holds slot 1. After the officer (soldier 1) dies, the survivors are
    // soldier 0 (slot 2, array index 0) and soldier 2 (slot 1, array index
    // 2): the one with the LOWER previous slot sits LATER in the array. That
    // inversion is deliberate -- it is what makes this test discriminate slot
    // order from array-position order. An algorithm that (wrongly) sorted by
    // array position would put soldier 0 first; the correct algorithm, which
    // sorts by previous slotIndex, must put soldier 2 first.
    SoldierHot s = makeSoldiers({0, 0, 0}, {2, 0, 1});
    //                index:     0  1  2
    //                slot:      2  0  1   -> order is 1, 2, 0
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);
    REQUIRE(members[0] == 1);  // soldier 1 is the officer

    // Kill the officer and rebuild.
    s.state[1] = SoldierState::Dead;
    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 2);
    // Array-position ordering would wrongly pick soldier 0 first (it comes
    // first in the backing array). Slot ordering must pick soldier 2 first,
    // since it held the lower previous slot (1 < 2).
    CHECK(members[0] == 2);
    CHECK(members[1] == 0);
    CHECK(s.slotIndex[2] == 0);
    CHECK(s.slotIndex[0] == 1);
}

TEST_CASE("equal previous slotIndex ties break on soldier index, not std::sort's whim") {
    // Every soldier spawns with slotIndex 0, so on the first-ever rebuild an
    // entire squad's sort keys are equal. std::sort is introsort, not a
    // stable sort: with an all-equal range its output permutation is
    // implementation-defined, and libstdc++ and MSVC STL disagree on it. The
    // comparator must tie-break on soldier index so the result is the same
    // on every platform.
    //
    // The squad MUST be large enough to enter introsort's partitioning path,
    // which is what actually permutes equal elements: libstdc++ routes
    // ranges under 16 elements, and MSVC STL ranges under roughly 32, straight
    // to insertion sort instead. Insertion sort only moves an element when
    // the comparator returns true, and an all-false comparator (no
    // tie-break) performs zero swaps on an already-ascending input, so a
    // small squad here passes identically with or without the fix and guards
    // nothing. 64 elements clears both cutoffs with margin. Do not shrink
    // this back down; a future "simplification" to a handful of soldiers
    // would silently disarm the test.
    //
    // NOTE (see task-4-report.md for the full trace): on MSVC STL this test
    // was verified NOT to fail even with the tie-break removed, at any size
    // tried (64 / 1000 / 100000). Reading vcruntime's <algorithm> shows why:
    // its partition routine first extends an "already equal" run from both
    // ends of the range, and with an all-false comparator that run always
    // meets in the middle, so the function returns before its swap loops
    // ever execute -- zero swaps, for any N. That is a genuine property of
    // MSVC's implementation, not a weak test. libstdc++'s partition (a Hoare
    // scheme keyed the same way) does not have that short-circuit and does
    // swap on an all-equal range, so this test still guards CI's
    // ubuntu-latest leg, which is the platform the original finding named.
    constexpr size_t kCount = 64;
    std::vector<uint16_t> squadIds(kCount, 0);
    std::vector<uint16_t> slotIndices(kCount, 0);  // all tied
    SoldierHot s = makeSoldiers(squadIds, slotIndices);
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    REQUIRE(members.size() == kCount);
    for (uint32_t i = 0; i < kCount; ++i) {
        CHECK(members[i] == i);
    }
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
