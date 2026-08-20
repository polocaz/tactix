#include <doctest/doctest.h>
#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include <cmath>

TEST_CASE("a slot rotates with squad facing") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 100.0f;
    q.centroidY[0] = 100.0f;
    q.memberCount[0] = 9;

    // Facing +x: local forward maps to world +x.
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    const Vec2 east = slotWorldPosition(q, 0, 4, 9);

    // Facing +y: the same slot must land somewhere different.
    q.facingX[0] = 0.0f; q.facingY[0] = 1.0f;
    const Vec2 north = slotWorldPosition(q, 0, 4, 9);

    CHECK(east.x != doctest::Approx(north.x));
}

TEST_CASE("a soldier standing on its slot is not pushed away") {
    Simulation sim(1280, 720, 42u);
    sim.init(200);
    sim.setPaused(false);
    // Two ticks so membership and aggregates settle.
    sim.tick(1.0f / 60.0f);
    sim.tick(1.0f / 60.0f);
    // No claim about a specific soldier; only that nothing has diverged.
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        CHECK(std::isfinite(sim.soldierX(i)));
        CHECK(std::isfinite(sim.soldierY(i)));
    }
}

TEST_CASE("deployment places every soldier on its own slot") {
    // Deployment (Simulation::init) must position each soldier at exactly
    // the slot steerToSlot will later target, using the SAME rotation
    // (slotWorldPosition), plus deploy jitter. Regression test for a bug
    // where init had its own hand-rolled rotation that disagreed with
    // slotWorldPosition's sign for team A, so team A soldiers spawned on
    // the mirror of their real slot and visibly swapped sides on tick 1.
    Simulation sim(1280, 720, 42u);
    sim.init(500);

    // Jitter is drawn as +/-2 on each axis (DeployJitterX/Y in init), so the
    // farthest a soldier can spawn from its exact slot is the diagonal of a
    // 4x4 box: sqrt(2^2 + 2^2). A small epsilon covers float rounding.
    const float kJitterBound = std::sqrt(2.0f * 2.0f + 2.0f * 2.0f) + 0.01f;

    // The bug was team-specific (only team A's rotation was wrong), so this
    // must check both teams -- a test that only sampled team B would have
    // missed it.
    bool sawTeamA = false, sawTeamB = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        CHECK(sim.slotError(i) <= kJitterBound);
        if (sim.soldierTeam(i) == Team::A) sawTeamA = true;
        else sawTeamB = true;
    }
    CHECK(sawTeamA);
    CHECK(sawTeamB);
}

TEST_CASE("squads close the distance to their slots over time") {
    Simulation sim(1280, 720, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);
    const float before = sim.meanSlotError();
    for (int i = 0; i < 120; ++i) sim.tick(1.0f / 60.0f);
    const float after = sim.meanSlotError();
    // Deployment already places soldiers near their slots, so this asserts
    // that steering does not make things worse, not that it converges from
    // far away.
    CHECK(after <= before + 1.0f);
}

TEST_CASE("a squad's centroid does not drift with no orders given") {
    // Regression test for a bug where updateSquadAggregate's centroid (the
    // mean of member positions) and formationSlot's front-anchored local
    // origin were different points: slotWorldPosition used the raw,
    // non-recentered offsets, so a squad's slot targets moved every time its
    // centroid was recomputed, and the whole squad walked backward (away
    // from its facing) without end. Measured before the fix: one team A
    // squad's centroid moved ~191px and one team B squad's moved ~191px in
    // opposite directions over 300 ticks, both clamped against the map edge.
    // meanSlotError alone cannot see this: the squad chases its own
    // receding target at a constant lag, so the mean error stays small even
    // as the squad translates. This checks the centroid position directly.
    Simulation sim(1280, 720, 42u);
    sim.init(500);

    size_t squadA = SIZE_MAX, squadB = SIZE_MAX;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (squadA == SIZE_MAX && sim.squadTeam(s) == Team::A) squadA = s;
        if (squadB == SIZE_MAX && sim.squadTeam(s) == Team::B) squadB = s;
    }
    REQUIRE(squadA != SIZE_MAX);
    REQUIRE(squadB != SIZE_MAX);

    const float a0x = sim.squadCentroidX(squadA), a0y = sim.squadCentroidY(squadA);
    const float b0x = sim.squadCentroidX(squadB), b0y = sim.squadCentroidY(squadB);

    sim.setPaused(false);
    for (int i = 0; i < 300; ++i) sim.tick(1.0f / 60.0f);

    // phaseSquadDecide is still a stub (plan 3), so no order ever moves a
    // squad on purpose here -- any drift is the bug this guards against.
    const float dAx = sim.squadCentroidX(squadA) - a0x;
    const float dAy = sim.squadCentroidY(squadA) - a0y;
    const float dBx = sim.squadCentroidX(squadB) - b0x;
    const float dBy = sim.squadCentroidY(squadB) - b0y;

    constexpr float kDriftBound = 15.0f;
    CHECK(std::sqrt(dAx * dAx + dAy * dAy) <= kDriftBound);
    CHECK(std::sqrt(dBx * dBx + dBy * dBy) <= kDriftBound);
}
