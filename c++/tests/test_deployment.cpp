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

TEST_CASE("both armies contain all three unit types even at a small agent count") {
    // 500 is not a round number picked for convenience: it is the measured
    // failure point of the bucket-cycle composition bug this guards
    // against (sq % 20 needed 13+ squads per team, ~601 agents, before an
    // archer ever appeared; 500 agents produced zero archers and zero
    // cavalry on both teams). Do not raise this number "to make the test
    // faster" -- that silently disarms the regression it exists to catch,
    // since the GUI's agent slider starts well below 601.
    Simulation sim(1280, 720, 42u);
    sim.init(500);

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

namespace {
void countByTeam(const Simulation& sim, uint32_t (&a)[kUnitTypeCount], uint32_t (&b)[kUnitTypeCount]) {
    for (uint32_t u = 0; u < kUnitTypeCount; ++u) { a[u] = 0; b[u] = 0; }
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        uint32_t* c = (sim.soldierTeam(i) == Team::A) ? a : b;
        c[(int)sim.soldierUnitType(i)]++;
    }
}
} // namespace

TEST_CASE("both armies have identical unit-type composition at 100 agents") {
    // 100 is the GUI's default agent count AND its slider minimum -- exactly
    // 2 squads per team. The measured failure point for finding 3: a naive
    // 60/85 percent split puts archerStart and cavalryStart on the same
    // bucket at squadsPerTeam == 2, so the old formula fielded Infantry +
    // Cavalry with ZERO archers, even though the whole point of scaling
    // composition by squad count was for a user watching the default battle
    // to see archers.
    Simulation sim(1280, 720, 42u);
    sim.init(100);

    uint32_t a[kUnitTypeCount], b[kUnitTypeCount];
    countByTeam(sim, a, b);
    for (uint32_t u = 0; u < kUnitTypeCount; ++u) CHECK(a[u] == b[u]);
    CHECK(a[(int)UnitType::Archer] > 0);
    CHECK(a[(int)UnitType::Infantry] > 0);
}

TEST_CASE("both armies have matching unit-type composition at 101 agents despite the odd split") {
    // 101 is the measured failure point for the SECOND finding-3 defect:
    // team A gets floor(101/2) = 50 soldiers, team B gets 51. 50 is an exact
    // multiple of kSquadSize (25), so team A needs 2 squads while team B's
    // extra soldier tips it to 3 -- squadsPerTeamA=2, squadsPerTeamB=3. The
    // old code fed each team its OWN squadsPerTeam into unitTypeForSquad, so
    // squad index 1 came out Cavalry for team A (squadsPerTeam==2 special
    // case) but Archer for team B (squadsPerTeam==3), giving the two armies
    // entirely different rosters -- a violation of the spec's two-symmetric-
    // armies requirement, independent of the zero-archer bug above.
    //
    // The fix derives ONE shared squadsPerTeam (the larger of the two) and
    // uses it for both teams' composition, so squad index N always means the
    // same type on both sides; team A just does not reach as high an index.
    // Deploying every one of the 101 agents (see "an odd soldier count
    // deploys the full count requested" above) means the one leftover
    // soldier unavoidably lands in exactly one bucket on team B alone, so
    // exact equality does not hold for that single bucket -- allow it a
    // difference of at most 1, and require every other bucket to match
    // exactly.
    Simulation sim(1280, 720, 42u);
    sim.init(101);

    uint32_t a[kUnitTypeCount], b[kUnitTypeCount];
    countByTeam(sim, a, b);
    uint32_t totalDiff = 0;
    for (uint32_t u = 0; u < kUnitTypeCount; ++u) {
        const uint32_t diff = (a[u] > b[u]) ? (a[u] - b[u]) : (b[u] - a[u]);
        CHECK(diff <= 1);
        totalDiff += diff;
    }
    CHECK(totalDiff == 1);  // exactly the one leftover soldier from the odd total
}

TEST_CASE("both armies field all three unit types at 150 agents (3 squads per team)") {
    // 150 agents = 75/team = exactly 3 squads per team on both sides (no odd
    // split), the smallest count where unitTypeForSquad's 3+ branch (as
    // opposed to the 2-squad special case above) is exercised for both
    // teams, and the case finding 3 gives as the CORRECT baseline all three
    // unit-type tests are measured against.
    Simulation sim(1280, 720, 42u);
    sim.init(150);

    uint32_t a[kUnitTypeCount], b[kUnitTypeCount];
    countByTeam(sim, a, b);
    for (uint32_t u = 0; u < kUnitTypeCount; ++u) {
        CHECK(a[u] > 0);
        CHECK(b[u] == a[u]);
    }
}

TEST_CASE("deployment is deterministic for a seed") {
    Simulation a(1280, 720, 7u);
    Simulation b(1280, 720, 7u);
    a.init(500);
    b.init(500);
    CHECK(a.stateDigest() == b.stateDigest());
}

TEST_CASE("deployment never spawns a soldier inside an obstacle") {
    // Design §11.1 / §14.1: no soldier spawned by init may be inside a
    // building or tree after jitter. Sweep several seeds and scales; the
    // safe-deployment fix (§5.1) clears each jittered slot, so this must
    // hold even for a soldier whose jitter would otherwise land on a wall.
    for (uint32_t seed : {1u, 7u, 42u}) {
        Simulation sim(1280, 720, seed);
        sim.init(2000);
        for (size_t i = 0; i < sim.getAgentCount(); ++i) {
            CHECK_FALSE(sim.insideAnyObstacle({sim.soldierX(i), sim.soldierY(i)}));
        }
    }
}

TEST_CASE("jittered deployment is obstacle-cleared") {
    // Design §11.1: the final spawned position -- after jitter AND clearing --
    // is outside every obstacle's standoff. Equivalent to the no-inside sweep
    // above but stated as a property of the cleared point itself on a field
    // that actually contains buildings and trees.
    Simulation sim(1280, 720, 42u);
    sim.init(10000);
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const Vec2 p{sim.soldierX(i), sim.soldierY(i)};
        CHECK_FALSE(sim.insideAnyObstacle(p));
        // And the cleared point must itself be the clear point: idempotent.
        const Vec2 again = sim.clearOfObstacles(p);
        CHECK(std::abs(again.x - p.x) < 1e-3f);
        CHECK(std::abs(again.y - p.y) < 1e-3f);
    }
}
