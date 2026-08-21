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
