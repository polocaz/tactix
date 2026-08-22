#include <doctest/doctest.h>
#include "Projectiles.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Rng.hpp"
#include <algorithm>
#include <cmath>

TEST_CASE("spawning a projectile appends to every parallel array") {
    ProjectileHot p;
    CHECK(p.count == 0);
    p.spawn(10.0f, 20.0f, 1.0f, 2.0f, Team::A, 1, 3.0f, 0.0f);
    p.spawn(30.0f, 40.0f, 3.0f, 4.0f, Team::B, 2, 4.0f, 0.0f);

    CHECK(p.count == 2);
    CHECK(p.posX.size() == 2);
    CHECK(p.posY.size() == 2);
    CHECK(p.velX.size() == 2);
    CHECK(p.velY.size() == 2);
    CHECK(p.team.size() == 2);
    CHECK(p.damage.size() == 2);
    CHECK(p.lifetime.size() == 2);
    CHECK(p.intentHitTarget.size() == 2);

    CHECK(p.posX[1] == doctest::Approx(30.0f));
    CHECK(p.team[1] == Team::B);
    CHECK(p.intentHitTarget[0] == UINT32_MAX);
}

TEST_CASE("a fresh simulation has no projectiles in flight") {
    Simulation sim(1280, 720, 42u);
    sim.init(200);
    CHECK(sim.getProjectileCount() == 0);
}

TEST_CASE("an arrow leaves at arrow speed and roughly toward the target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Archer, 0);
    s.intentFire[0] = 1;
    s.spawn(300.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);

    SquadHot q;
    q.spawn(Team::A, UnitType::Archer);
    q.targetSoldier[0] = 1;

    ProjectileHot p;
    const Rng rng{42u, 1u};
    spawnArrows(s, q, p, rng);

    REQUIRE(p.count == 1);
    const float speed = std::sqrt(p.velX[0] * p.velX[0] + p.velY[0] * p.velY[0]);
    CHECK(speed == doctest::Approx(kArrowSpeed).epsilon(0.01));
    // Target is due east, so the arrow must fly broadly east.
    CHECK(p.velX[0] > 0.0f);
    CHECK(p.team[0] == Team::A);
}

TEST_CASE("spread is bounded and deterministic") {
    // Same seed and tick must give the same shot every time, and the angle
    // must stay inside the configured spread.
    auto fire = [](uint32_t tick) {
        SoldierHot s;
        s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Archer, 0);
        s.intentFire[0] = 1;
        s.spawn(300.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
        SquadHot q;
        q.spawn(Team::A, UnitType::Archer);
        q.targetSoldier[0] = 1;
        ProjectileHot p;
        spawnArrows(s, q, p, Rng{42u, tick});
        return p;
    };

    const ProjectileHot a = fire(1u);
    const ProjectileHot b = fire(1u);
    REQUIRE(a.count == 1);
    CHECK(a.velX[0] == b.velX[0]);
    CHECK(a.velY[0] == b.velY[0]);

    // Angle off due east must be within the base spread plus the distance
    // widening. Derived from spawnArrows' own formula (Projectiles.cpp)
    // rather than a pasted number: shot distance is 200px (300 - 100), the
    // shooter here is stationary (shooterSpeed == 0, so that factor drops
    // out), and maxRange comes from the archer's own UnitStats -- giving
    // spreadMrad = 40 * (1 + 200/280) ~= 68.57, truncated to 68, i.e. a true
    // bound of 0.068 rad. A 10% margin over that (still ~7x tighter than the
    // old flat 0.5f, which was loose enough to pass even with excess spread)
    // is enough to absorb the int truncation without actually being blind to
    // a regression.
    const float dist = 300.0f - 100.0f;
    const float maxRange = kUnitStats[(int)UnitType::Archer].range;
    const float maxSpread = (float)kArrowBaseSpreadMrad * (1.0f + dist / maxRange) * 0.001f;
    const float angle = std::atan2(a.velY[0], a.velX[0]);
    CHECK(std::fabs(angle) < maxSpread * 1.1f);
}

TEST_CASE("a dead archer does not shoot") {
    // Mirrors "a dead attacker does not swing" in test_combat.cpp: resolution
    // steps 1-2 can zero a soldier's health earlier in the same tick, but
    // state is not set to Dead until step 4 (recordCasualties), which runs
    // AFTER spawnArrows (step 3). Without the health guard in spawnArrows, a
    // soldier killed earlier this same tick still looses an arrow.
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Archer, 0);
    s.intentFire[0] = 1;
    s.health[0] = 0;
    s.spawn(300.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);

    SquadHot q;
    q.spawn(Team::A, UnitType::Archer);
    q.targetSoldier[0] = 1;

    ProjectileHot p;
    spawnArrows(s, q, p, Rng{42u, 1u});

    CHECK(p.count == 0);
}

TEST_CASE("a segment through a circle is a hit") {
    CHECK(segmentHitsCircle(0.0f, 0.0f, 10.0f, 0.0f, 5.0f, 0.0f, 1.0f));
}

TEST_CASE("a segment passing beside a circle is a miss") {
    CHECK_FALSE(segmentHitsCircle(0.0f, 0.0f, 10.0f, 0.0f, 5.0f, 50.0f, 1.0f));
}

TEST_CASE("a segment stopping short of a circle is a miss") {
    CHECK_FALSE(segmentHitsCircle(0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 1.0f));
}

TEST_CASE("a swept test catches a target a point test would tunnel through") {
    // This is the case that forced the swept test. An arrow at 200px/s and a
    // cavalryman crossing at 95px/s give a relative displacement of about
    // 4.9px per tick against a 4px radius, so sampling only the endpoints
    // misses a target the arrow demonstrably passed through.
    const float x0 = 0.0f, y0 = 0.0f;
    const float x1 = 4.9f, y1 = 0.0f;
    const float cx = 2.45f, cy = 0.0f;
    const float r = kSoldierRadius * 0.5f;

    // A point test sampling either endpoint alone would miss: the segment's
    // midpoint sits exactly on the target's centre, so both the start and
    // the end of the tick's movement are equidistant from it and that
    // distance exceeds the radius. Checking both endpoints (not just one)
    // is what actually demonstrates neither point test would catch this --
    // relying on the symmetry of this particular case to imply the second
    // from the first would leave that half of the claim unverified.
    const float d0 = std::sqrt((cx - x0) * (cx - x0) + (cy - y0) * (cy - y0));
    const float d1 = std::sqrt((cx - x1) * (cx - x1) + (cy - y1) * (cy - y1));
    CHECK(d0 > r);
    CHECK(d1 > r);

    // The swept test still finds it: the segment passes directly through
    // the target's centre even though neither endpoint is inside it.
    CHECK(segmentHitsCircle(x0, y0, x1, y1, cx, cy, r));
}

TEST_CASE("an arrow expires when its lifetime runs out") {
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, Team::A, 1, 0.01f, 0.0f);
    SoldierHot s;
    SpatialHash hash(1280.0f, 720.0f, 50.0f);
    std::vector<uint32_t> scratch;

    integrateProjectile(p, s, hash, 0, 1.0f / 60.0f, scratch);
    CHECK(p.lifetime[0] <= 0.0f);
}

TEST_CASE("no target means no arrow") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Archer, 0);
    s.intentFire[0] = 1;
    SquadHot q;
    q.spawn(Team::A, UnitType::Archer);
    q.targetSoldier[0] = UINT32_MAX;

    ProjectileHot p;
    spawnArrows(s, q, p, Rng{42u, 1u});
    CHECK(p.count == 0);
}

TEST_CASE("an arrow that reaches a soldier rolls to wound and is spent either way") {
    // Contact is no longer a guaranteed wound (kArrowHitChancePct). What must
    // hold for every arrow is that it is SPENT on contact -- a glance that
    // stayed alive would re-roll next tick and make the chance meaningless.
    int landed = 0;
    const int shots = 400;
    for (uint32_t tick = 1; tick <= (uint32_t)shots; ++tick) {
        SoldierHot s;
        s.spawn(100.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 0);
        const uint8_t before = s.health[0];

        ProjectileHot p;
        p.spawn(100.0f, 100.0f, 0.0f, 0.0f, Team::A, kArrowDamage, 1.0f, 0.0f);
        p.intentHitTarget[0] = 0;

        applyProjectileHits(p, s, Rng{42u, tick});

        CHECK(p.lifetime[0] <= 0.0f);
        const bool hit = s.health[0] == before - kArrowDamage;
        CHECK((hit || s.health[0] == before));  // never anything in between
        if (hit) ++landed;
    }

    // Wide band on purpose: this guards that the roll is wired up and roughly
    // centred on the constant, not that 400 samples hit it exactly.
    CHECK(landed > shots * (kArrowHitChancePct - 15) / 100);
    CHECK(landed < shots * (kArrowHitChancePct + 15) / 100);
}

TEST_CASE("an arrow cannot finish off an already dead soldier") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 0);
    s.health[0] = 0;

    ProjectileHot p;
    p.spawn(100.0f, 100.0f, 0.0f, 0.0f, Team::A, kArrowDamage, 1.0f, 0.0f);
    p.intentHitTarget[0] = 0;

    applyProjectileHits(p, s, Rng{42u, 1u});
    CHECK(s.health[0] == 0);  // no underflow to 255
}

namespace {
// Every ProjectileHot vector must stay exactly count long. Listing them
// explicitly, mirroring test_combat.cpp's checkArraysConsistent: when a
// field is added to ProjectileHot, this list is the one place a compiler
// will not remind you to update.
void checkProjectileArraysConsistent(const ProjectileHot& p) {
    CHECK(p.posX.size() == p.count);
    CHECK(p.posY.size() == p.count);
    CHECK(p.velX.size() == p.count);
    CHECK(p.velY.size() == p.count);
    CHECK(p.team.size() == p.count);
    CHECK(p.damage.size() == p.count);
    CHECK(p.lifetime.size() == p.count);
    CHECK(p.intentHitTarget.size() == p.count);
}

// Mirrors test_combat.cpp's spawnFingerprinted/checkFingerprintIntact: gives
// projectile k a distinctive value in every one of the 8 ProjectileHot
// fields, each independently encoding the same k, so a swap that moves 7
// fields correctly and drops the 8th is detectable even though every
// vector's LENGTH still matches count.
void spawnProjectileFingerprinted(ProjectileHot& p, int k) {
    p.spawn(100.0f + (float)k, 200.0f + (float)k, 300.0f + (float)k, 400.0f + (float)k,
           (k % 2 == 0) ? Team::A : Team::B, (uint8_t)(k + 1), 500.0f + (float)k,
           600.0f + (float)k);
    p.intentHitTarget[p.count - 1] = (uint32_t)(1000 + k);
}

void checkProjectileFingerprintIntact(const ProjectileHot& p, size_t i) {
    const int k = (int)std::lround(p.posX[i] - 100.0f);
    CAPTURE(i);
    CAPTURE(k);
    CHECK(p.posY[i]           == doctest::Approx(200.0f + k));
    CHECK(p.velX[i]           == doctest::Approx(300.0f + k));
    CHECK(p.velY[i]           == doctest::Approx(400.0f + k));
    CHECK(p.team[i]           == ((k % 2 == 0) ? Team::A : Team::B));
    CHECK(p.damage[i]         == (uint8_t)(k + 1));
    CHECK(p.lifetime[i]       == doctest::Approx(500.0f + k));
    CHECK(p.intentHitTarget[i] == (uint32_t)(1000 + k));
}
} // namespace

TEST_CASE("spent and expired arrows are removed") {
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 1.0f, 0.0f);
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, -0.1f, 0.0f);  // expired
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 2.0f, 0.0f);

    compactProjectiles(p);

    CHECK(p.count == 2);
    checkProjectileArraysConsistent(p);
    for (size_t i = 0; i < p.count; ++i) CHECK(p.lifetime[i] > 0.0f);
}

TEST_CASE("compaction moves every projectile field together, not just lengths") {
    // Two non-adjacent, non-tail arrows expire so the swap-with-back loop
    // has to pull survivors in from the back more than once, same as
    // test_combat.cpp's equivalent soldier-side test. Sizes alone (previous
    // test) cannot tell a correct swap from one that left a field behind;
    // the fingerprint can. damage and intentHitTarget in particular were
    // covered by NEITHER the state digest NOR any prior test here -- a
    // dropped damage.pop_back() desynced the arrays with no failure anywhere
    // in the repository.
    ProjectileHot p;
    const int N = 6;
    for (int k = 0; k < N; ++k) spawnProjectileFingerprinted(p, k);
    p.lifetime[1] = -0.1f;
    p.lifetime[3] = -0.1f;

    compactProjectiles(p);

    CHECK(p.count == (size_t)(N - 2));
    checkProjectileArraysConsistent(p);
    for (size_t i = 0; i < p.count; ++i) checkProjectileFingerprintIntact(p, i);
}

TEST_CASE("an archer cannot fire on consecutive ticks") {
    // The attackCooldown-setting loop after spawnArrows in
    // Simulation::phaseResolution had zero test coverage: in the committed
    // 200-tick baseline, archers fire once at tick 1 and never reacquire, so
    // deleting that loop entirely does not move the digest. This exercises
    // it directly. Field is deliberately small (kept well under archer range
    // 280px between the two starting lines) so archers on both sides are
    // already in range from the very first tick, instead of needing hundreds
    // of ticks to close distance first.
    Simulation sim(300, 300, 42u);
    sim.init(100);  // 2 squads/team: infantry + archer, see unitTypeForSquad
    sim.setPaused(false);

    // Tick 1 is its own transient: every squad's targetSquad still holds its
    // spawn default (0) until phase 3 (selectTargetSquad) runs for the first
    // time, so only whichever side's archer squad happens to default-target
    // a real enemy fires this tick (see selectTargetSoldier's same-team
    // guard, finding 1). Tick 2 is where the OTHER side's archer squad, now
    // correctly retargeted, fires for the first time. By the end of tick 2
    // every in-range archer squad has fired exactly once and is on cooldown
    // -- that is the settled state this test actually checks.
    sim.tick(1.0f / 60.0f);
    sim.tick(1.0f / 60.0f);
    const size_t afterSettling = sim.getProjectileCount();
    REQUIRE(afterSettling > 0);

    sim.tick(1.0f / 60.0f);
    // kArcherCooldown (1.5s) is far longer than one 1/60s tick, and arrows
    // already in flight have moved only ~6.7px against a >150px gap, so none
    // have hit or expired to mask growth by shrinking the count. Without the
    // cooldown guard, every archer that fired tick 2 would fire again here.
    CHECK(sim.getProjectileCount() <= afterSettling);
}

TEST_CASE("arrows exist and are consumed in a running battle") {
    // 1600 ticks, not 1200: test_combat.cpp measured the first genuine
    // (cross-team, in-range) target acquisition at this same 500-agent,
    // 2400x1600 configuration at tick 1333, now that squads no longer
    // acquire a same-team target on tick 1's transient (finding 1) --
    // archers cannot fire before they have a real target. 1200 ticks
    // predates that and would see zero shots; 1600 gives margin past it.
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    size_t peak = 0;
    for (int i = 0; i < 1600; ++i) {
        sim.tick(1.0f / 60.0f);
        peak = std::max(peak, sim.getProjectileCount());
    }
    // Archers must actually shoot, and the array must not grow without bound.
    CHECK(peak > 0);
    CHECK(sim.getProjectileCount() < peak * 4 + 100);
}

namespace {
// One arrow flying +x from the origin, with a single soldier of the given team
// planted at `fraction` of the way to a target `dist` away. Returns the arrow's
// intentHitTarget after flying far enough to reach that soldier.
uint32_t flyPast(Team arrowTeam, Team soldierTeam, float dist, float fraction) {
    SoldierHot soldiers;
    soldiers.spawn(dist * fraction, 0.0f, 0.0f, 0.0f, soldierTeam,
                   UnitType::Infantry, 0);

    SpatialHash hash(1280.0f, 720.0f, 50.0f);
    hash.insert(0u, soldiers.posX[0], soldiers.posY[0]);

    ProjectileHot p;
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, arrowTeam, kArrowDamage,
            kArrowLifetime, kArrowArcFraction * dist);

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    for (int t = 0; t < 600 && p.posX[0] <= dist * fraction + 20.0f; ++t) {
        integrateProjectile(p, soldiers, hash, 0, dt, scratch);
        if (p.intentHitTarget[0] != UINT32_MAX) break;
    }
    return p.intentHitTarget[0];
}
} // namespace

TEST_CASE("an arrow passes harmlessly over anyone under its arc") {
    // 20 percent along, well inside kArrowArcFraction.
    CHECK(flyPast(Team::A, Team::A, 400.0f, 0.2f) == UINT32_MAX);
    CHECK(flyPast(Team::A, Team::B, 400.0f, 0.2f) == UINT32_MAX);
}

TEST_CASE("an arrow is live near the target and hits either team") {
    CHECK(flyPast(Team::A, Team::B, 400.0f, 0.95f) == 0u);
    // The whole point: your own men near the impact are NOT safe.
    CHECK(flyPast(Team::A, Team::A, 400.0f, 0.95f) == 0u);
}

TEST_CASE("a point-blank shot is live almost immediately") {
    // liveAfter scales with the shot distance, so close range is direct fire.
    CHECK(flyPast(Team::A, Team::B, 30.0f, 0.9f) == 0u);
}

TEST_CASE("traveled accumulates with flight distance") {
    SoldierHot soldiers;
    SpatialHash hash(1280.0f, 720.0f, 50.0f);
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, Team::A, kArrowDamage,
            kArrowLifetime, 1e9f);   // never arms, so it just flies

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    for (int t = 0; t < 30; ++t) integrateProjectile(p, soldiers, hash, 0, dt, scratch);

    CHECK(p.traveled[0] == doctest::Approx(kArrowSpeed * dt * 30.0f).epsilon(1e-3));
}

TEST_CASE("compaction moves the arc fields with everything else") {
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 0.0f, 111.0f);   // expired
    p.spawn(5.0f, 0.0f, 1.0f, 0.0f, Team::B, 1, 1.0f, 222.0f);   // alive
    p.traveled[1] = 33.0f;

    compactProjectiles(p);
    REQUIRE(p.count == 1);
    CHECK(p.liveAfter[0] == doctest::Approx(222.0f));
    CHECK(p.traveled[0]  == doctest::Approx(33.0f));
}
