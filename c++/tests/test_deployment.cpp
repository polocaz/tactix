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

TEST_CASE("deployment is deterministic for a seed") {
    Simulation a(1280, 720, 7u);
    Simulation b(1280, 720, 7u);
    a.init(500);
    b.init(500);
    CHECK(a.stateDigest() == b.stateDigest());
}
