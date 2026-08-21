#include <doctest/doctest.h>
#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include <cmath>

TEST_CASE("a slot rotates with squad facing") {
    // Asserts the actual expected world coordinates for a known facing and
    // slot, not just "some difference exists" -- east.x != north.x alone
    // does not discriminate handedness, and a mirrored rotation (the exact
    // Task 8 bug: team A's slots came out mirrored) would still pass it.
    // Expected values worked out by hand from formationSlot's Line layout
    // (width 5 for 9 members, spacing 12) and slotWorldPosition's
    // recentering and rotation:
    //   raw slot 4 (row 0, col 4): right=24, forward=0
    //   formationMeanOffset(Line, 9): mean=(-8/3, -16/3)
    //   local = raw - mean = (80/3, 16/3)
    //   facing east (1,0): rightX=0, rightY=-1 -> world = centroid + (local.y, -local.x)
    //   facing north (0,1): rightX=1, rightY=0 -> world = centroid + (local.x, local.y)
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 100.0f;
    q.centroidY[0] = 100.0f;
    q.memberCount[0] = 9;

    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    const Vec2 east = slotWorldPosition(q, 0, 4, 9);
    CHECK(east.x == doctest::Approx(100.0f + 16.0f / 3.0f));
    CHECK(east.y == doctest::Approx(100.0f - 80.0f / 3.0f));

    q.facingX[0] = 0.0f; q.facingY[0] = 1.0f;
    const Vec2 north = slotWorldPosition(q, 0, 4, 9);
    CHECK(north.x == doctest::Approx(100.0f + 80.0f / 3.0f));
    CHECK(north.y == doctest::Approx(100.0f + 16.0f / 3.0f));
}

// A test named "a soldier standing on its slot is not pushed away" used to sit
// here. It was removed rather than repaired, because it could not fail.
//
// It ticked twice and asserted meanSlotError stayed under 10px. Deployment
// already places every soldier within ~2.83px of its slot, and two ticks of
// separation move a soldier a fraction of a pixel, so the assertion held with
// steerToSlot deleted entirely. Verified by stubbing the call site: the test
// passed. Widening the bound or ticking longer would only have turned it into a
// duplicate of "meanSlotError plateaus instead of drifting" below.
//
// The property it claimed to guard IS guarded, in two places that provably fail
// without steering: the plateau test and the centroid drift test, both further
// down this file. Stubbing steerToSlot fails both, plus the committed baseline.
// Four direct unit tests of steerToSlot at the top of this file cover the
// function itself. A third integration test asserting the same thing under a
// different name added nothing but the appearance of coverage.

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

// The four cases below call steerToSlot directly, constructing SoldierHot
// and SquadHot by hand (the pattern test_squads.cpp uses), rather than going
// through Simulation. They exist because the plateau test and the drift
// test below CANNOT tell a working steerToSlot from a no-op: deployment now
// places every soldier within jitter of its slot by calling
// slotWorldPosition directly, so with steerToSlot deleted entirely,
// soldiers simply stand where they spawned -- zero drift, a perfect
// plateau, every bound trivially satisfied, and the headline behaviour of
// this task absent. These tests isolate convergence from deployment
// accuracy by starting a soldier well away from its slot and checking
// steerToSlot's output directly.

TEST_CASE("steerToSlot points toward the slot when displaced") {
    // Kills a no-op steerToSlot (velocity would stay zero) and a sign error
    // in the rotation or the (dx, dy) direction (velocity would point away
    // from the slot instead of toward it).
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 100.0f; q.centroidY[0] = 100.0f;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.memberCount[0] = 1;  // a single-member squad's only slot sits exactly on the centroid

    SoldierHot s;
    s.spawn(40.0f, 20.0f, 0.0f, 0.0f, Team::A, UnitType::Infantry, 0);
    // Displaced (60, 80) from its slot, magnitude 100px: well outside the
    // approach-easing zone (speed*dt*4 = 3px for infantry at 60fps).
    steerToSlot(s, q, 0, 1.0f / 60.0f);

    const float speedSq = s.velX[0] * s.velX[0] + s.velY[0] * s.velY[0];
    CHECK(speedSq > 0.0f);

    const float toSlotX = 60.0f, toSlotY = 80.0f;
    const float dot = s.velX[0] * toSlotX + s.velY[0] * toSlotY;
    CHECK(dot > 0.0f);
}

TEST_CASE("steerToSlot moves at the unit's full speed when far away") {
    // Guards against steering that points the right way but crawls: past
    // the approach-easing zone, velocity magnitude must equal the unit's
    // rated speed exactly, not some fraction of it.
    SquadHot q;
    q.spawn(Team::A, UnitType::Cavalry);
    q.centroidX[0] = 0.0f; q.centroidY[0] = 0.0f;
    q.facingX[0] = 0.0f; q.facingY[0] = 1.0f;
    q.memberCount[0] = 1;

    SoldierHot s;
    s.spawn(100.0f, 0.0f, 0.0f, 0.0f, Team::A, UnitType::Cavalry, 0);
    steerToSlot(s, q, 0, 1.0f / 60.0f);

    const float speed = std::sqrt(s.velX[0] * s.velX[0] + s.velY[0] * s.velY[0]);
    CHECK(speed == doctest::Approx(kUnitStats[(int)UnitType::Cavalry].speed));
}

TEST_CASE("steerToSlot zeroes velocity for a soldier already on its slot") {
    // Guards the arrival deadband, which is what stops 10000 soldiers
    // shimmering in place once they arrive. Only implicitly covered
    // elsewhere: deployment starts soldiers near their slot but this is the
    // only test that puts one EXACTLY on it and checks the result is a hard
    // zero, not a tiny noisy nonzero velocity.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 50.0f; q.centroidY[0] = 50.0f;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.memberCount[0] = 1;

    SoldierHot s;
    // Spawned with a deliberately nonzero velocity: a no-op steerToSlot
    // would leave this untouched, so this is what makes the assertion
    // below load-bearing rather than trivially true of an unstarted
    // soldier that already has zero velocity.
    s.spawn(50.0f, 50.0f, 3.0f, -4.0f, Team::A, UnitType::Infantry, 0);  // exactly on its slot
    steerToSlot(s, q, 0, 1.0f / 60.0f);

    CHECK(s.velX[0] == 0.0f);
    CHECK(s.velY[0] == 0.0f);
}

TEST_CASE("repeated steerToSlot monotonically closes the distance") {
    // The actual "soldiers close the distance to their slots" property,
    // isolated from deployment accuracy. Starts a soldier 100px from its
    // slot and integrates position by hand (velocity times dt), calling
    // steerToSlot every step -- no Simulation involved, so this cannot be
    // satisfied by deployment alone the way the plateau test below can.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 200.0f; q.centroidY[0] = 200.0f;
    q.facingX[0] = 0.0f; q.facingY[0] = -1.0f;
    q.memberCount[0] = 1;

    SoldierHot s;
    s.spawn(200.0f - 60.0f, 200.0f - 80.0f, 0.0f, 0.0f, Team::A, UnitType::Infantry, 0);
    const float dt = 1.0f / 60.0f;

    auto distToSlot = [&]() {
        const float dx = 200.0f - s.posX[0];
        const float dy = 200.0f - s.posY[0];
        return std::sqrt(dx * dx + dy * dy);
    };

    float prevDist = distToSlot();
    REQUIRE(prevDist == doctest::Approx(100.0f));

    bool reachedDeadband = false;
    for (int i = 0; i < 400; ++i) {
        steerToSlot(s, q, 0, dt);
        s.posX[0] += s.velX[0] * dt;
        s.posY[0] += s.velY[0] * dt;

        const float dist = distToSlot();
        if (i < 20) {
            // Strictly decreasing while well outside the deadband: no
            // overshoot, no stall, no oscillation.
            CHECK(dist < prevDist);
        }
        prevDist = dist;
        if (dist <= 2.0f) { reachedDeadband = true; break; }
    }
    CHECK(reachedDeadband);
}

TEST_CASE("meanSlotError plateaus instead of drifting") {
    // This used to assert convergence toward zero error. That assumption
    // does not hold: generateObstacles() scatters 8 buildings and 30 trees
    // across the field, and both armies deploy down the field's full height
    // (Simulation::init), so some soldiers' assigned slots land inside or
    // beside an obstacle. Obstacle avoidance correctly and permanently holds
    // those soldiers off their slot -- you cannot stand inside a wall -- so
    // a nonzero mean error is the correct steady state, not a defect.
    //
    // Confirmed directly: with obstacles (as shipped), meanSlotError plateaus
    // around tick120=8.63 / tick400=8.02. With generateObstacles() disabled
    // for the same seed and agent count, it collapses to tick120=1.63 /
    // tick400=1.53, near steerToSlot's 2px arrival deadband. The gap between
    // those two runs is obstacles, not a steering defect, so what is worth
    // guarding is that the error reaches a STABLE plateau, not that it goes
    // to zero. If this test starts failing because the plateau crept back
    // down near zero, that means obstacles stopped blocking slots (a
    // deployment or generateObstacles change), not a steering regression --
    // do not "fix" this back into a zero-convergence assertion.
    Simulation sim(1280, 720, 42u);
    sim.init(500);
    sim.setPaused(false);

    for (int i = 0; i < 120; ++i) sim.tick(1.0f / 60.0f);
    const float at120 = sim.meanSlotError();

    for (int i = 0; i < 280; ++i) sim.tick(1.0f / 60.0f);
    const float at400 = sim.meanSlotError();

    // Measured delta between tick120 and tick400 was ~0.61px; 2px gives
    // over 3x headroom while still catching a plateau that has not
    // actually settled (e.g. still climbing toward an unbounded drift).
    CHECK(std::fabs(at400 - at120) <= 2.0f);

    // Measured plateau was ~8.0-8.6px; 15px gives comfortable headroom
    // above that without being so loose it would pass the ~16px+ plateau
    // seen before the separation-radius fix, or the far larger figures
    // the centroid-drift and rotation bugs produced earlier in this task.
    CHECK(at400 <= 15.0f);
}

// Formerly "a squad's centroid does not drift with no orders given": a
// regression test for a bug where updateSquadAggregate's centroid and
// formationSlot's front-anchored local origin were different points, which
// made a squad walk backward without end. It asserted near-zero centroid
// movement over 300 ticks on the premise that phaseSquadDecide was still a
// stub and no order ever moved a squad on purpose.
//
// That premise is exactly what this task (squads advance on the nearest
// enemy squad) removes: every squad now gets a live Advance order and its
// centroid is *supposed* to move, so the old assertion fails by design, not
// by regression. The bug it guarded against is a different mechanism
// (formationMeanOffset recentring, unchanged by this task) and stays covered
// by "meanSlotError plateaus instead of drifting" above (would catch an
// unbounded lag between soldiers and their slots) and by
// test_advance.cpp's "advancing does not tear the formation apart" (would
// catch the formation stretching without bound) and "the armies close the
// distance between them" (would catch a squad failing to advance at all).
