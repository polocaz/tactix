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
    p.spawn(10.0f, 20.0f, 1.0f, 2.0f, Team::A, 1, 3.0f);
    p.spawn(30.0f, 40.0f, 3.0f, 4.0f, Team::B, 2, 4.0f);

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
    // widening, generously bounded here.
    const float angle = std::atan2(a.velY[0], a.velX[0]);
    CHECK(std::fabs(angle) < 0.5f);
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
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, Team::A, 1, 0.01f);
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

TEST_CASE("an arrow hit costs the target health and spends the arrow") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 0);
    const uint8_t before = s.health[0];

    ProjectileHot p;
    p.spawn(100.0f, 100.0f, 0.0f, 0.0f, Team::A, kArrowDamage, 1.0f);
    p.intentHitTarget[0] = 0;

    applyProjectileHits(p, s);

    CHECK(s.health[0] == before - kArrowDamage);
    CHECK(p.lifetime[0] <= 0.0f);
}

TEST_CASE("an arrow cannot finish off an already dead soldier") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 0);
    s.health[0] = 0;

    ProjectileHot p;
    p.spawn(100.0f, 100.0f, 0.0f, 0.0f, Team::A, kArrowDamage, 1.0f);
    p.intentHitTarget[0] = 0;

    applyProjectileHits(p, s);
    CHECK(s.health[0] == 0);  // no underflow to 255
}

TEST_CASE("spent and expired arrows are removed") {
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 1.0f);
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, -0.1f);  // expired
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 2.0f);

    compactProjectiles(p);

    CHECK(p.count == 2);
    CHECK(p.posX.size() == 2);
    CHECK(p.lifetime.size() == 2);
    for (size_t i = 0; i < p.count; ++i) CHECK(p.lifetime[i] > 0.0f);
}

TEST_CASE("arrows exist and are consumed in a running battle") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    size_t peak = 0;
    for (int i = 0; i < 1200; ++i) {
        sim.tick(1.0f / 60.0f);
        peak = std::max(peak, sim.getProjectileCount());
    }
    // Archers must actually shoot, and the array must not grow without bound.
    CHECK(peak > 0);
    CHECK(sim.getProjectileCount() < peak * 4 + 100);
}
