#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "Combat.hpp"
#include <cmath>

TEST_CASE("a soldier never targets its own team") {
    // 1200x800 field: measured first contact (any soldier acquiring a live
    // melee target) at tick 559 for this seed/agent count. 900 ticks gives
    // comfortable margin past that (165/500 soldiers engaged by then).
    Simulation sim(1200, 800, 42u);
    sim.init(500);
    sim.setPaused(false);
    // Long enough for the armies to make contact.
    for (int i = 0; i < 900; ++i) sim.tick(1.0f / 60.0f);

    bool anyTarget = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const uint32_t t = sim.soldierIntentTarget(i);
        if (t == UINT32_MAX) continue;
        anyTarget = true;
        REQUIRE(t < sim.getAgentCount());
        CHECK(sim.soldierTeam(t) != sim.soldierTeam(i));
    }
    // If nothing ever engaged, the test proved nothing.
    CHECK(anyTarget);
}

TEST_CASE("a soldier only targets what is within melee reach") {
    Simulation sim(1200, 800, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 900; ++i) sim.tick(1.0f / 60.0f);

    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const uint32_t t = sim.soldierIntentTarget(i);
        if (t == UINT32_MAX) continue;
        const float dx = sim.soldierX(t) - sim.soldierX(i);
        const float dy = sim.soldierY(t) - sim.soldierY(i);
        CHECK(std::sqrt(dx * dx + dy * dy) <= kMeleeReach + 0.01f);
    }
}
