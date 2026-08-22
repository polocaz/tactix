#include <doctest/doctest.h>
#include "Army.hpp"
#include "Squads.hpp"
#include "Units.hpp"
#include "Simulation.hpp"
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

namespace {
size_t countRole(const SquadHot& q, Team t, SquadRole r) {
    size_t n = 0;
    for (size_t s = 0; s < q.count; ++s) {
        if (q.team[s] == t && q.role[s] == (uint8_t)r) n++;
    }
    return n;
}
} // namespace

TEST_CASE("every archer squad is told to shoot") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Archer,   100.0f, 140.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 120.0f, 20);
    f.add(Team::B, UnitType::Infantry, 900.0f, 120.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Shoot) == 2);
}

TEST_CASE("a threatened archer squad gets exactly one screen") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 260.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 320.0f, 100.0f, 20);
    // An enemy well inside kScreenThreatRadius of the archers.
    f.add(Team::B, UnitType::Infantry, 100.0f + kScreenThreatRadius * 0.5f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 1);
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.role[s] == (uint8_t)SquadRole::Screen) {
            CHECK(f.squads.wardSquad[s] == archers);
        }
    }
}

TEST_CASE("an unthreatened archer squad gets no screen") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 100.0f + kScreenThreatRadius * 3.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 0);
}

TEST_CASE("an army with no archers assigns no screens") {
    ArmyFixture f;
    for (int i = 0; i < 4; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    f.add(Team::B, UnitType::Infantry, 300.0f, 160.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 0);
}

TEST_CASE("line squads spread across enemies instead of piling on the nearest") {
    // The regression test for the actual complaint. Four equal infantry
    // squads against four equal enemies: each enemy should draw one.
    ArmyFixture f;
    for (int i = 0; i < 4; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    std::vector<uint16_t> enemies;
    for (int i = 0; i < 4; ++i) {
        enemies.push_back(f.add(Team::B, UnitType::Infantry,
                                800.0f, 100.0f + (float)i * 40.0f, 20));
    }
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    std::vector<int> load(f.squads.count, 0);
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] == Team::A && f.squads.role[s] == (uint8_t)SquadRole::Line) {
            load[f.squads.targetSquad[s]]++;
        }
    }
    for (uint16_t e : enemies) {
        CHECK(load[e] == 1);
    }
}

TEST_CASE("a stronger enemy squad draws proportionally more attackers") {
    ArmyFixture f;
    for (int i = 0; i < 6; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    const uint16_t big   = f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 60);
    const uint16_t small = f.add(Team::B, UnitType::Infantry, 800.0f, 300.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    int bigLoad = 0, smallLoad = 0;
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] != Team::A) continue;
        if (f.squads.role[s] != (uint8_t)SquadRole::Line) continue;
        if (f.squads.targetSquad[s] == big)   bigLoad++;
        if (f.squads.targetSquad[s] == small) smallLoad++;
    }
    CHECK(bigLoad > smallLoad);
}

TEST_CASE("cavalry are sent to flank") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Cavalry,  200.0f, 100.0f, 10);
    f.add(Team::A, UnitType::Infantry, 200.0f, 200.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 200.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Flank) == 1);
}

TEST_CASE("every squad ends up with a live enemy target") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Cavalry,  200.0f, 200.0f, 10);
    const uint16_t enemy = f.add(Team::B, UnitType::Infantry, 800.0f, 150.0f, 20);
    f.add(Team::B, UnitType::Infantry, 850.0f, 150.0f, 0);   // wiped out
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] != Team::A) continue;
        CHECK(f.squads.targetSquad[s] == enemy);   // the only live one
    }
}

TEST_CASE("assignment leaves an army with no live enemies untouched") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 0);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);   // must not crash or hang
    CHECK(f.squads.count == 2);
}

TEST_CASE("assignment is deterministic from identical input") {
    auto build = []() {
        auto f = std::make_unique<ArmyFixture>();
        for (int i = 0; i < 5; ++i) {
            f->add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 37.0f, 18 + i);
        }
        f->add(Team::A, UnitType::Archer,  120.0f, 180.0f, 12);
        f->add(Team::A, UnitType::Cavalry, 210.0f, 300.0f, 10);
        for (int i = 0; i < 4; ++i) {
            f->add(Team::B, UnitType::Infantry, 800.0f, 90.0f + (float)i * 41.0f, 15 + i * 3);
        }
        updateArmyAggregate(f->squads, f->armies);
        assignRoles(f->squads, f->armies, Team::A);
        return f;
    };

    auto a = build();
    auto b = build();
    for (size_t s = 0; s < a->squads.count; ++s) {
        CHECK(a->squads.role[s]        == b->squads.role[s]);
        CHECK(a->squads.targetSquad[s] == b->squads.targetSquad[s]);
        CHECK(a->squads.wardSquad[s]   == b->squads.wardSquad[s]);
    }
}

TEST_CASE("roles are assigned on the very first tick, not on the stagger") {
    // The stagger must not leave a squad acting on a role it never received.
    Simulation sim(1280, 720, 42u);
    sim.init(600);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    size_t shooters = 0, archers = 0;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (sim.squadUnitType(s) == UnitType::Archer) {
            archers++;
            if (sim.squadRole(s) == (uint8_t)SquadRole::Shoot) shooters++;
        }
    }
    REQUIRE(archers > 0);
    CHECK(shooters == archers);
}

TEST_CASE("an engaged squad is given the Engaged order, not Advance") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    bool sawEngaged = false;
    for (int t = 0; t < 1600 && !sawEngaged; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t s = 0; s < sim.getSquadCount(); ++s) {
            if (sim.squadContact(s)) {
                CHECK(sim.squadOrder(s) != (uint8_t)SquadOrder::Advance);
                sawEngaged = true;
            }
        }
    }
    CHECK(sawEngaged);
}

TEST_CASE("a screening squad puts itself between its ward and the threat") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);
    for (int t = 0; t < 900; ++t) sim.tick(1.0f / 60.0f);

    size_t checked = 0;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (sim.squadRole(s) != (uint8_t)SquadRole::Screen) continue;
        const uint16_t ward = sim.squadWardSquad(s);
        const uint16_t threat = sim.squadTargetSquad(s);
        REQUIRE(ward != UINT16_MAX);

        // The objective must be closer to the threat than the ward is: that is
        // what 'between' means operationally.
        const float ox = sim.squadObjectiveX(s), oy = sim.squadObjectiveY(s);
        const float tx = sim.squadCentroidX(threat), ty = sim.squadCentroidY(threat);
        const float wx = sim.squadCentroidX(ward),   wy = sim.squadCentroidY(ward);

        const float objToThreat = std::sqrt((ox - tx) * (ox - tx) + (oy - ty) * (oy - ty));
        const float wardToThreat = std::sqrt((wx - tx) * (wx - tx) + (wy - ty) * (wy - ty));
        CHECK(objToThreat < wardToThreat);
        checked++;
    }
    REQUIRE(checked > 0);
}

TEST_CASE("thread count still does not change simulation state") {
    // The new serial army phase and the role-driven decide must not have
    // introduced any dependence on chunking.
    auto run = [](uint32_t threads) {
        Simulation sim(1280, 720, 42u, threads);
        sim.init(2000);
        sim.setPaused(false);
        for (int t = 0; t < 900; ++t) sim.tick(1.0f / 60.0f);
        return sim.stateDigest();
    };
    const uint64_t single = run(1u);
    CHECK(run(2u)  == single);
    CHECK(run(8u)  == single);
    CHECK(run(15u) == single);
}
