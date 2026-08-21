#include <doctest/doctest.h>
#include "Simulation.hpp"
#include <cmath>

TEST_CASE("each squad targets an enemy squad, never a friendly one") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    REQUIRE(sim.getSquadCount() > 1);
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const uint16_t t = sim.squadTargetSquad(s);
        REQUIRE(t < sim.getSquadCount());
        CHECK(sim.squadTeam(t) != sim.squadTeam(s));
    }
}

TEST_CASE("the armies close the distance between them") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    const float startGap = std::fabs(sim.teamCentroidX(Team::B) - sim.teamCentroidX(Team::A));
    for (int i = 0; i < 600; ++i) sim.tick(1.0f / 60.0f);
    const float endGap = std::fabs(sim.teamCentroidX(Team::B) - sim.teamCentroidX(Team::A));

    // Ten seconds of marching must visibly close the gap. Without an advance
    // offset the squads hold formation forever and this stays flat, which is
    // exactly the state this task exists to fix.
    CHECK(endGap < startGap - 100.0f);
}

TEST_CASE("advancing does not tear the formation apart") {
    // The advance offset pulls soldiers forward off their slots. If it is too
    // large they never catch up and the formation stretches without bound.
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 300; ++i) sim.tick(1.0f / 60.0f);
    CHECK(sim.meanSlotError() <= 25.0f);
}
