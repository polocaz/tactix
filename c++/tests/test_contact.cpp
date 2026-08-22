#include <doctest/doctest.h>
#include "Contact.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "SpatialHash.hpp"
#include "Formation.hpp"
#include <cmath>
#include <vector>

namespace {

// A minimal two-squad fixture: squad 0 is team A infantry, squad 1 is team B
// infantry, each with `n` members laid out in a straight line along x at the
// given origin. Bypasses Simulation so contact can be tested in isolation.
struct Fixture {
    SoldierHot soldiers;
    SquadHot squads;
    std::vector<uint32_t> members;
    SpatialHash hash{ 1280.0f, 720.0f, 50.0f };

    void addSquad(Team team, float originX, float originY, uint32_t n, float spacing) {
        const uint16_t sq = (uint16_t)squads.count;
        squads.spawn(team, UnitType::Infantry);
        squads.memberStart[sq] = (uint32_t)members.size();
        squads.memberCount[sq] = n;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t idx = (uint32_t)soldiers.count;
            soldiers.spawn(originX + (float)k * spacing, originY,
                           0.0f, 0.0f, team, UnitType::Infantry, sq);
            soldiers.slotIndex[idx] = (uint16_t)k;
            members.push_back(idx);
        }
    }

    void rehash() {
        hash.clear();
        for (size_t i = 0; i < soldiers.count; ++i) {
            hash.insert((uint32_t)i, soldiers.posX[i], soldiers.posY[i]);
        }
    }
};

} // namespace

TEST_CASE("a squad with no enemy nearby is not in contact") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 600.0f, 100.0f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}

TEST_CASE("a squad whose front rank meets the enemy enters contact") {
    Fixture f;
    // Two lines facing each other, well inside kContactRadius.
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 1);
}

TEST_CASE("one lone skirmisher in reach does not put a whole squad in contact") {
    Fixture f;
    // 60 members, so rankOfSlot puts 11 of them in rank 0 (rankWidth at aspect
    // 2.0 is ceil(sqrt(120))). A single enemy reaches two of those eleven,
    // which is 0.18 and below kContactFraction, so the formation keeps
    // marching. Sized off the real front-rank width rather than off the squad
    // size: a 20-man squad has a 7-wide front, and two of seven is 0.29, which
    // is ABOVE the threshold. The lone-skirmisher case only exists at all when
    // the front rank is wide enough for one man to be a small fraction of it.
    f.addSquad(Team::A, 100.0f, 100.0f, 60, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 1, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}

TEST_CASE("contact does not clear until the grace period expires") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);

    // Kill the enemy squad outright, then tick. Contact must persist through
    // the grace period and only then clear.
    for (uint32_t k = 0; k < f.squads.memberCount[1]; ++k) {
        f.soldiers.state[f.members[f.squads.memberStart[1] + k]] = SoldierState::Dead;
    }

    const int graceTicks = (int)(kContactClearSeconds / dt);
    for (int t = 0; t < graceTicks - 1; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 1);   // still latched

    for (int t = 0; t < 3; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 0);   // grace expired
}

TEST_CASE("an empty squad is never in contact and does not divide by zero") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 0, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}
