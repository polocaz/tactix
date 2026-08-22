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

TEST_CASE("a free squad's anchor tracks its centroid exactly") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 600.0f, 100.0f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 313.0f;
    f.squads.centroidY[0] = 207.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);

    REQUIRE(f.squads.contact[0] == 0);
    CHECK(f.squads.anchorX[0] == doctest::Approx(313.0f));
    CHECK(f.squads.anchorY[0] == doctest::Approx(207.0f));
}

TEST_CASE("a marching squad's anchor does not trail its centroid") {
    // The regression test for easing a FREE squad instead of only a releasing
    // one. A marching centroid moves about 0.75px per tick, and an ease at
    // dt/kAnchorReleaseSeconds settles to a permanent trailing error of
    // roughly 22px, which drags every formation slot backward. Exact tracking
    // is the requirement, not a nicety.
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 900.0f, 100.0f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 100.0f;
    f.squads.centroidY[0] = 100.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    const float perTick = kUnitStats[(int)UnitType::Infantry].speed * dt;

    for (int t = 0; t < 300; ++t) {
        f.squads.centroidX[0] += perTick;
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
        REQUIRE(f.squads.contact[0] == 0);
        CHECK(f.squads.anchorX[0] == doctest::Approx(f.squads.centroidX[0]));
    }
}

TEST_CASE("the anchor latches on the rising edge of contact and then holds") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 150.0f;
    f.squads.centroidY[0] = 100.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);
    CHECK(f.squads.anchorX[0] == doctest::Approx(150.0f));

    // The centroid now drifts, as it would while men shuffle in a melee. The
    // anchor must NOT follow it: that is the whole point.
    f.squads.centroidX[0] = 400.0f;
    f.squads.centroidY[0] = 400.0f;
    for (int t = 0; t < 10; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 1);
    CHECK(f.squads.anchorX[0] == doctest::Approx(150.0f));
    CHECK(f.squads.anchorY[0] == doctest::Approx(100.0f));
}

TEST_CASE("the anchor eases back to the centroid after contact clears") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 100.0f;
    f.squads.centroidY[0] = 100.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);

    for (uint32_t k = 0; k < f.squads.memberCount[1]; ++k) {
        f.soldiers.state[f.members[f.squads.memberStart[1] + k]] = SoldierState::Dead;
    }
    f.squads.centroidX[0] = 200.0f;   // 100px away from the latched anchor

    // Run out the contact grace period, then the release period.
    const int ticks = (int)((kContactClearSeconds + kAnchorReleaseSeconds * 4.0f) / dt);
    for (int t = 0; t < ticks; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }

    CHECK(f.squads.contact[0] == 0);
    // Eased, not snapped: it must have closed most of the gap, but the point
    // is that it arrives smoothly rather than in one frame.
    CHECK(f.squads.anchorX[0] == doctest::Approx(200.0f).epsilon(0.02));
}

TEST_CASE("two advancing squads do not pass through each other") {
    // The regression test for the spin (design 2.1). Before anchor latching,
    // both centroids converge on one point and the formations orbit it.
    Simulation sim(1280, 720, 42u);
    sim.init(400);
    sim.setPaused(false);

    const float startGap = std::abs(sim.teamCentroidX(Team::A) - sim.teamCentroidX(Team::B));
    const bool aStartsLeft = sim.teamCentroidX(Team::A) < sim.teamCentroidX(Team::B);
    REQUIRE(startGap > 100.0f);

    for (int t = 0; t < 1200; ++t) sim.tick(1.0f / 60.0f);

    // Whichever side started on the left must still be on the left. Passing
    // through would flip the sign; orbiting a shared point would collapse the
    // gap to near zero.
    const bool aStillLeft = sim.teamCentroidX(Team::A) < sim.teamCentroidX(Team::B);
    CHECK(aStillLeft == aStartsLeft);
}
