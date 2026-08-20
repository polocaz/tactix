#include <doctest/doctest.h>
#include "Simulation.hpp"

TEST_CASE("both armies get roughly half the soldiers") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    const size_t a = sim.getTeamCount(Team::A);
    const size_t b = sim.getTeamCount(Team::B);
    CHECK(a + b == sim.getAgentCount());
    // Squads are whole, so the split is not exact.
    CHECK(a > 400);
    CHECK(b > 400);
}

TEST_CASE("armies deploy on opposite sides") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    // Team A occupies the left third, team B the right third.
    CHECK(sim.teamCentroidX(Team::A) < 1280.0f / 3.0f);
    CHECK(sim.teamCentroidX(Team::B) > 1280.0f * 2.0f / 3.0f);
}

TEST_CASE("deployment scales with map size") {
    Simulation wide(4000, 2500, 42u);
    wide.init(1000);
    CHECK(wide.teamCentroidX(Team::B) > 4000.0f * 2.0f / 3.0f);

    // Every soldier must be inside the world, whatever its size.
    for (size_t i = 0; i < wide.getAgentCount(); ++i) {
        CHECK(wide.soldierX(i) >= 0.0f);
        CHECK(wide.soldierX(i) <= 4000.0f);
        CHECK(wide.soldierY(i) >= 0.0f);
        CHECK(wide.soldierY(i) <= 2500.0f);
    }
}

TEST_CASE("every soldier belongs to a squad that claims it") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    CHECK(sim.getSquadCount() > 0);
    // Membership is rebuilt on the first tick.
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);
    CHECK(sim.everySoldierHasASquadSlot());
}

TEST_CASE("an odd soldier count deploys the full count requested") {
    Simulation sim(1280, 720, 42u);
    sim.init(1001);
    CHECK(sim.getAgentCount() == 1001);
    const size_t a = sim.getTeamCount(Team::A);
    const size_t b = sim.getTeamCount(Team::B);
    CHECK(a + b == 1001);
}

TEST_CASE("no squad origin falls outside the world, even when deployment cannot fit") {
    // Regression test for the finding-1 pile-up bug: an unbounded column
    // pitch put team A's squads as far as x=-928 for 10000 agents on a
    // 1280x720 field, which clampf then piled onto the world boundary.
    // 10000 soldiers genuinely do not fit in 1280x720 at full formation
    // spacing (see the "packed tighter" warning deployment emits for this
    // exact case) -- the property worth guarding is that deployment
    // degrades in bounds instead of spilling past them.
    Simulation sim(1280, 720, 42u);
    sim.init(10000);
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        CHECK(sim.squadCentroidX(s) >= 0.0f);
        CHECK(sim.squadCentroidX(s) <= 1280.0f);
        CHECK(sim.squadCentroidY(s) >= 0.0f);
        CHECK(sim.squadCentroidY(s) <= 720.0f);
    }
}

TEST_CASE("both armies contain all three unit types") {
    // Regression guard: if shapeForUnit or the deployment bucket logic ever
    // collapsed to a single unit type (e.g. shapeForUnit returning Line for
    // everything), nothing else would fail -- Loose and Wedge would simply
    // never be exercised end to end through Simulation::init. 2000 agents
    // gives ~40 squads per team, comfortably past the 20-squad bucket cycle
    // (60% infantry / 25% archer / 15% cavalry) that guarantees all three
    // appear for both teams.
    Simulation sim(1280, 720, 42u);
    sim.init(2000);

    bool sawA[kUnitTypeCount] = {false, false, false};
    bool sawB[kUnitTypeCount] = {false, false, false};
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        bool* saw = (sim.soldierTeam(i) == Team::A) ? sawA : sawB;
        saw[(int)sim.soldierUnitType(i)] = true;
    }
    for (uint32_t u = 0; u < kUnitTypeCount; ++u) {
        CHECK(sawA[u]);
        CHECK(sawB[u]);
    }
}

TEST_CASE("deployment is deterministic for a seed") {
    Simulation a(1280, 720, 7u);
    Simulation b(1280, 720, 7u);
    a.init(500);
    b.init(500);
    CHECK(a.stateDigest() == b.stateDigest());
}
