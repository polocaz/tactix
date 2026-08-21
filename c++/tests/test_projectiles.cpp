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
