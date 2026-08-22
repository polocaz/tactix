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
    // slotWorldPosition builds from the ANCHOR, not the centroid (design 5.2).
    // For a free squad the two are equal, and detectContact keeps them so
    // every tick; a hand-built SquadHot has to establish that itself.
    q.anchorX[0] = 100.0f;
    q.anchorY[0] = 100.0f;
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
    // Deployment (Simulation::init) must position each soldier at the slot
    // steerToSlot will later target, using the SAME rotation
    // (slotWorldPosition), plus deploy jitter, then obstacle-clearing.
    // Regression test for a bug where init had its own hand-rolled rotation
    // that disagreed with slotWorldPosition's sign for team A, so team A
    // soldiers spawned on the mirror of their real slot and visibly swapped
    // sides on tick 1.
    Simulation sim(1280, 720, 42u);
    sim.init(500);

    // Jitter is drawn as +/-2 on each axis (DeployJitterX/Y in init), so the
    // farthest a soldier can be nudged by jitter alone is the diagonal of a
    // 4x4 box: sqrt(2^2 + 2^2).
    const float kJitterBound = std::sqrt(2.0f * 2.0f + 2.0f * 2.0f) + 0.01f;

    // Design §5.1: deploy jitter is applied to the raw slot and THEN the
    // result is obstacle-cleared (the safe-deployment fix). A jittered point
    // can therefore be pushed back out to the standoff boundary, moving it up
    // to kObstacleStandoff from the cleared raw slot it is steered toward.
    // That distance is transient initial noise -- it is NOT baked into the
    // persistent slot target (§5.2) -- so the bound is jitter + standoff, not
    // jitter alone. A rotation-mirror bug would land soldiers tens to hundreds
    // of px off their slot (the formation is far wider than this), so this
    // still catches the bug it was written for.
    const float kSpawnBound = kJitterBound + kObstacleStandoff + 0.5f;

    // The bug was team-specific (only team A's rotation was wrong), so this
    // must check both teams -- a test that only sampled team B would have
    // missed it.
    bool sawTeamA = false, sawTeamB = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        CHECK(sim.slotError(i) <= kSpawnBound);
        // And the spawn itself is obstacle-cleared (design §11.1 / §14.1).
        CHECK_FALSE(sim.insideAnyObstacle({sim.soldierX(i), sim.soldierY(i)}));
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
    q.anchorX[0] = 100.0f; q.anchorY[0] = 100.0f;  // anchor is the formation origin now
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
    q.anchorX[0] = 0.0f; q.anchorY[0] = 0.0f;  // anchor is the formation origin now
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
    q.anchorX[0] = 50.0f; q.anchorY[0] = 50.0f;  // anchor is the formation origin now
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
    q.anchorX[0] = 200.0f; q.anchorY[0] = 200.0f;  // anchor is the formation origin now
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
    // This used to assert convergence toward zero error. That assumption does
    // not hold, and the reason has changed twice, so it is worth being exact
    // about what this now guards.
    //
    // Originally the error was dominated by obstacles: some slots landed
    // inside a building and no soldier can stand in a wall, which pinned it
    // at tick120=8.63 / tick400=8.02 against 1.6 with generateObstacles()
    // disabled. Simulation::clearOfObstacles now slides those slots out to
    // kObstacleStandoff, so that gap is largely gone.
    //
    // What dominates instead is combat: squads lose members, slot layouts
    // renumber under them, and every squad under an Advance order carries a
    // steady kAdvanceLead lag. Measured after the clearOfObstacles fix, with
    // obstacles: 8.9 / 4.8 / 8.1 / 5.9 at ticks 120/400/700/1000. The SAME
    // run with generateObstacles() disabled gives 5.1 / 7.3 / 20.8 / 30.2 --
    // higher late, not lower, because the armies close and grind sooner
    // without terrain in the way.
    //
    // So this is a bound, not a convergence: the error stays in a bounded
    // band rather than drifting without limit. Do not "fix" it back into a
    // zero-convergence assertion, and do not read a few px of movement in
    // either direction as a steering regression.
    Simulation sim(1280, 720, 42u);
    sim.init(500);
    sim.setPaused(false);

    for (int i = 0; i < 120; ++i) sim.tick(1.0f / 60.0f);
    const float at120 = sim.meanSlotError();

    for (int i = 0; i < 280; ++i) sim.tick(1.0f / 60.0f);
    const float at400 = sim.meanSlotError();

    // Measured band is ~4.8-9px; 15px gives comfortable headroom above that
    // without being so loose it would pass the ~16px+ plateau seen before
    // the separation-radius fix, or the far larger figures the
    // centroid-drift and rotation bugs produced earlier in this task.
    // Both checkpoints are bounded rather than compared to each other: the
    // metric genuinely swings a few px as squads take casualties, so a tight
    // delta between two arbitrary ticks would be a flake, not a guard.
    CHECK(at120 <= 15.0f);
    CHECK(at400 <= 15.0f);
}

// Formerly "a squad's centroid does not drift with no orders given", which
// asserted near-zero centroid movement over 300 ticks on the premise that
// phaseSquadDecide was still a stub and no order ever moved a squad on
// purpose. This task (squads advance on the nearest enemy squad) removes
// that premise: every squad now gets a live Advance order, and its centroid
// is *supposed* to move, so the old assertion fails by design, not by
// regression.
//
// The mechanism it guarded is unrelated to Advance, though, and stays real:
// a squad's centroid is the MEAN of member positions (updateSquadAggregate),
// while formationSlot's local origin is the formation's front-center, a
// different point. Without formationMeanOffset recentring that gap away,
// slot targets shift every time the centroid is recomputed and the whole
// squad walks off without end even on Hold. meanSlotError does NOT catch
// this: a soldier chases its own receding target at a roughly constant lag,
// so mean per-soldier error stays small the entire time the squad is
// marching off the map -- which is exactly why the original ~191px/300-tick
// bug was found by measuring centroid displacement, not the error plateau.
// The replacement below does the same: build a Hold squad in isolation, run
// steerToSlot by hand, and watch the centroid, not the per-soldier error.
TEST_CASE("a squad on Hold does not drift: centroid stays formationMeanOffset's fixed point") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.order[0] = (uint8_t)SquadOrder::Hold;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    const float startX = 500.0f, startY = 300.0f;
    q.centroidX[0] = startX; q.centroidY[0] = startY;
    q.anchorX[0] = startX; q.anchorY[0] = startY;  // anchor is the formation origin now

    constexpr uint32_t kMembers = 12;
    q.memberCount[0] = kMembers;

    SoldierHot soldiers;
    for (uint32_t k = 0; k < kMembers; ++k) {
        // Placed exactly on its slot, as deployment does, so any movement
        // that follows is the drift under test, not arrival transient.
        const Vec2 slot = slotWorldPosition(q, 0, (uint16_t)k, kMembers);
        soldiers.spawn(slot.x, slot.y, 0.0f, 0.0f, Team::A, UnitType::Infantry, 0);
        soldiers.slotIndex[k] = (uint16_t)k;
    }

    const float dt = 1.0f / 60.0f;
    for (int tick = 0; tick < 300; ++tick) {
        for (uint32_t k = 0; k < kMembers; ++k) {
            steerToSlot(soldiers, q, k, dt);
            soldiers.posX[k] += soldiers.velX[k] * dt;
            soldiers.posY[k] += soldiers.velY[k] * dt;
        }
        // Recompute the centroid exactly as updateSquadAggregate does: the
        // mean of member positions, summed in index order.
        float sumX = 0.0f, sumY = 0.0f;
        for (uint32_t k = 0; k < kMembers; ++k) {
            sumX += soldiers.posX[k];
            sumY += soldiers.posY[k];
        }
        q.centroidX[0] = sumX / (float)kMembers;
        q.centroidY[0] = sumY / (float)kMembers;
        // A free squad's anchor tracks its centroid exactly, which is what
        // detectContact does every tick in the live simulation. Mirroring it
        // here is what keeps this loop a faithful stand-in for a real tick.
        q.anchorX[0] = q.centroidX[0];
        q.anchorY[0] = q.centroidY[0];
    }

    const float dx = q.centroidX[0] - startX;
    const float dy = q.centroidY[0] - startY;
    // A Hold squad gets no advance lead, so its centroid is a fixed point by
    // construction. 5px is headroom for float accumulation over 300 ticks,
    // not room for controlled movement -- see the load-bearing check in the
    // commit message: stubbing formationMeanOffset to {0,0} reproduces the
    // original bug and fails this bound by roughly two orders of magnitude.
    CHECK(std::sqrt(dx * dx + dy * dy) <= 5.0f);
}

TEST_CASE("clearOfObstacles never returns a point inside an obstacle") {
    // The whole point of the function: a formation slot inside a wall is a
    // target no soldier can ever reach, so it gets slid out to somewhere one
    // can stand. Swept over the field rather than spot-checked, because the
    // case that used to break it -- a point wedged between two overlapping
    // buildings, pushed out of the first and straight into the second -- is
    // not one you would think to hand-pick.
    Simulation sim(1280, 720, 42u);
    sim.init(100);

    int movedAtLeastOne = 0;
    for (int y = 0; y <= 720; y += 7) {
        for (int x = 0; x <= 1280; x += 7) {
            const Vec2 raw{ (float)x, (float)y };
            const Vec2 clear = sim.clearOfObstacles(raw);
            CHECK_FALSE(sim.insideAnyObstacle(clear));
            if (clear.x != raw.x || clear.y != raw.y) ++movedAtLeastOne;
        }
    }

    // Guards the test itself: if generateObstacles ever stopped producing
    // obstacles, every point above would pass trivially and this test would
    // be measuring nothing.
    CHECK(movedAtLeastOne > 0);
}

TEST_CASE("a soldier standing on a cleared slot is not pushed off it") {
    // kObstacleStandoff is shared by clearOfObstacles and the avoidance push
    // in phaseSoldierSteerChunk precisely so that a soldier sent to a cleared
    // slot feels zero force there. When the clearance was smaller than the
    // avoidance reach, soldiers next to buildings were shoved off their slot,
    // walked back, and ground against the wall forever -- the bug this pairing
    // fixes. Measured end to end: after settling, no soldier sits absurdly far
    // from the point steering is actually sending it to.
    Simulation sim(1280, 720, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 200; ++i) sim.tick(1.0f / 60.0f);

    size_t stranded = 0;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        if (sim.slotError(i) > 60.0f) ++stranded;
    }
    // Before the fix this ran to dozens of soldiers pinned against walls at
    // 60px+; the bound is deliberately loose so ordinary marching lag and
    // squads manoeuvring around a building do not trip it.
    CHECK(stranded <= sim.getAgentCount() / 20);
}
