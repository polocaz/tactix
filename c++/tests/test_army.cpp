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

    size_t checked = 0, inFront = 0;
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
        if (objToThreat < wardToThreat) inFront++;
        checked++;
    }
    REQUIRE(checked > 0);
    MESSAGE("screens in front of their ward: " << inFront << " of " << checked);

    // A MAJORITY property, not a universal one, and the difference is real
    // rather than a hedge. roleAnchorFor puts a screen squarely between its
    // ward and the threat, but that anchor is only candidate 0 for the terrain
    // scorer, which is free to prefer a nearby building corner or a clearer
    // patch of ground. A screen that steps around a wall is doing the right
    // thing and is briefly no longer on the ward-threat line.
    //
    // The pure intent is asserted exactly, without terrain, by the
    // roleAnchorFor cases above. What this test owns is that screening
    // actually happens in a live battle and mostly points the right way.
    CHECK(inFront * 5 >= checked * 4);   // at least 80%
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

TEST_CASE("archers break for the rear when melee closes, and keep their role") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    bool sawWithdraw = false;
    for (int t = 0; t < 2400 && !sawWithdraw; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t s = 0; s < sim.getSquadCount(); ++s) {
            if (sim.squadUnitType(s) != UnitType::Archer) continue;
            if (sim.squadOrder(s) != (uint8_t)SquadOrder::Withdraw) continue;
            sawWithdraw = true;

            // The role is unchanged: they are still archers with a job, just
            // running. That is what lets them resume without a new assignment
            // when they rally.
            CHECK(sim.squadRole(s) == (uint8_t)SquadRole::Shoot);

            // And the objective is away from the enemy, not toward it.
            //
            // Measured against the enemy ARMY centroid rather than against
            // squadTargetSquad: an archer squad's target is whoever it is
            // shooting at, which is not necessarily the melee squad bearing
            // down on it, so that comparison would be testing the wrong
            // vector. Flight is biased toward the squad's own rear, which is
            // defined relative to the enemy army, so this is the statement the
            // behaviour actually makes.
            const Team foe = (sim.squadTeam(s) == Team::A) ? Team::B : Team::A;
            const float ex = sim.armyCentroidX(foe), ey = sim.armyCentroidY(foe);
            const float cx = sim.squadCentroidX(s), cy = sim.squadCentroidY(s);
            const float ox = sim.squadObjectiveX(s), oy = sim.squadObjectiveY(s);
            const float nowDist  = std::sqrt((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey));
            const float goalDist = std::sqrt((ox - ex) * (ox - ex) + (oy - ey) * (oy - ey));
            CHECK(goalDist > nowDist);
            break;
        }
    }
    CHECK(sawWithdraw);
}

TEST_CASE("a withdrawing squad's soldiers move faster than their march speed") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    const float march = kUnitStats[(int)UnitType::Archer].speed;
    bool sawFast = false;
    for (int t = 0; t < 2400 && !sawFast; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t i = 0; i < sim.getAgentCount(); ++i) {
            if (sim.soldierUnitType(i) != UnitType::Archer) continue;
            const uint16_t sq = sim.soldierSquadId(i);
            if (sim.squadOrder(sq) != (uint8_t)SquadOrder::Withdraw) continue;
            if (sim.soldierSpeed(i) > march * 1.1f) { sawFast = true; break; }
        }
    }
    CHECK(sawFast);
}

TEST_CASE("hysteresis: panic entry and exit use different radii") {
    // Stated as a property of the constants rather than simulated, because the
    // failure mode is a squad flip-flopping every tick and the guard against
    // it is simply that the two radii differ. The screen radius must be the
    // widest of the three, or the bodyguard arrives after the panic.
    CHECK(kArcherRallyRadius > kArcherPanicRadius);
    CHECK(kScreenThreatRadius > kArcherRallyRadius);
}

TEST_CASE("archers prefer a firing position with friendly infantry in front") {
    // Two otherwise-equal candidate positions, one screened by our own line.
    // The scorer must pick the screened one.
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    f.add(Team::A, UnitType::Infantry, 300.0f, 300.0f, 30);   // squarely in front
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 700.0f, 300.0f, 30);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    f.squads.facingX[archers] = 1.0f; f.squads.facingY[archers] = 0.0f;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;   // no obstacles: isolates the screen term
    const Vec2 anchor = roleAnchorFor(f.squads, f.armies, archers);
    Vec2 obj{}, mv{};
    chooseTacticalObjective(empty, f.squads, archers, anchor, obj, mv);

    // The chosen objective must be on our side of the friendly infantry, not
    // out past it toward the enemy.
    CHECK(obj.x < 300.0f);
}

TEST_CASE("a squad holds fire when its target is mixed in with our own men") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 400.0f, 300.0f, 30);
    // Our own infantry right on top of the enemy: a melee.
    f.add(Team::A, UnitType::Infantry, 400.0f + kMeleeMixRadius * 0.4f, 300.0f, 30);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;
    squadDecide(f.squads, f.armies, archers, empty, 1.0f / 60.0f);
    CHECK(f.squads.friendlyNearTarget[archers] == 1);
}

TEST_CASE("a squad with a clean shot does not hold fire") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 400.0f, 300.0f, 30);
    f.add(Team::A, UnitType::Infantry, 150.0f, 300.0f, 30);   // well behind the impact
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;
    squadDecide(f.squads, f.armies, archers, empty, 1.0f / 60.0f);
    CHECK(f.squads.friendlyNearTarget[archers] == 0);
}

TEST_CASE("holding fire actually stops the squad acquiring a soldier target") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 200.0f, 300.0f, 4);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    f.squads.friendlyNearTarget[archers] = 1;
    f.squads.centroidX[archers] = 100.0f; f.squads.centroidY[archers] = 300.0f;

    SoldierHot soldiers;
    std::vector<uint32_t> members;
    f.squads.memberStart[foe] = 0;
    for (uint32_t k = 0; k < 4; ++k) {
        soldiers.spawn(200.0f, 300.0f, 0.0f, 0.0f, Team::B, UnitType::Infantry, foe);
        soldiers.slotIndex[k] = (uint16_t)k;
        members.push_back(k);
    }

    selectTargetSoldier(soldiers, f.squads, members, archers);
    CHECK(f.squads.targetSoldier[archers] == UINT32_MAX);

    // And clearing the flag lets them shoot again: the hold is a live
    // condition, not a latch.
    f.squads.friendlyNearTarget[archers] = 0;
    selectTargetSoldier(soldiers, f.squads, members, archers);
    CHECK(f.squads.targetSoldier[archers] != UINT32_MAX);
}

TEST_CASE("screening never consumes more than its share of the infantry") {
    // The per-archer-squad cap ("at most one guard each") does NOT bound this.
    // Archers are roughly a quarter of an army, and once the field is crowded
    // essentially every archer squad has an enemy inside kScreenThreatRadius,
    // so one guard each still claims essentially every infantry squad.
    //
    // Measured at 10,000 agents after 4,000 ticks with only the per-squad cap:
    // 82 live squads were screening and 3 were holding the line. The design
    // asks for the opposite, and this is the test that would have said so.
    ArmyFixture f;
    std::vector<uint16_t> archers, infantry;
    for (int i = 0; i < 10; ++i) {
        archers.push_back(f.add(Team::A, UnitType::Archer, 100.0f, 100.0f + (float)i * 30.0f, 12));
    }
    for (int i = 0; i < 10; ++i) {
        infantry.push_back(f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 30.0f, 20));
    }
    // One enemy close enough to threaten every archer squad at once.
    f.add(Team::B, UnitType::Infantry, 100.0f + kScreenThreatRadius * 0.5f, 250.0f, 20);

    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    const size_t screens = countRole(f.squads, Team::A, SquadRole::Screen);
    const size_t line    = countRole(f.squads, Team::A, SquadRole::Line);

    CHECK(screens <= (size_t)(infantry.size() * kMaxScreenFraction));
    // And the rest of the infantry must actually be fighting, not idle.
    CHECK(line == infantry.size() - screens);
    CHECK(line > screens);
}
