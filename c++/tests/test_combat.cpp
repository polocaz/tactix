#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "Combat.hpp"
#include "SpatialHash.hpp"
#include <cmath>

namespace {
// Mirrors Simulation::rebuildSpatialHash: same cell size, same insertion
// order (soldier index), so selectMeleeTarget's tie-breaking is exercised
// the same way it would be inside a real tick.
SpatialHash buildHash(const SoldierHot& s, float w = 1200.0f, float h = 800.0f) {
    SpatialHash hash(w, h, 50.0f);
    hash.clear();
    for (size_t i = 0; i < s.count; ++i) {
        hash.insert(static_cast<uint32_t>(i), s.posX[i], s.posY[i]);
    }
    return hash;
}
} // namespace

TEST_CASE("selectMeleeTarget never picks a same-team soldier") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);  // 0: seeker
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);  // 1: friendly, in reach
    s.spawn(108.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);  // 2: enemy, in reach
    s.spawn(300.0f, 300.0f, 0, 0, Team::B, UnitType::Infantry, 1);  // 3: enemy, out of reach
    s.spawn(400.0f, 400.0f, 0, 0, Team::A, UnitType::Infantry, 0);  // 4: friendly, out of reach
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    for (size_t i = 0; i < s.count; ++i) {
        selectMeleeTarget(s, hash, i, scratch);
        const uint32_t t = s.intentTarget[i];
        if (t == UINT32_MAX) continue;
        CHECK(s.team[t] != s.team[i]);
    }
}

TEST_CASE("selectMeleeTarget never picks a target beyond melee reach") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(108.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    s.spawn(300.0f, 300.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    s.spawn(400.0f, 400.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    for (size_t i = 0; i < s.count; ++i) {
        selectMeleeTarget(s, hash, i, scratch);
        const uint32_t t = s.intentTarget[i];
        if (t == UINT32_MAX) continue;
        const float dx = s.posX[t] - s.posX[i];
        const float dy = s.posY[t] - s.posY[i];
        CHECK(std::sqrt(dx * dx + dy * dy) <= kMeleeReach + 0.01f);
    }
}

TEST_CASE("a soldier with an enemy just inside melee reach acquires it") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(100.0f + kMeleeReach - 0.1f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == 1);
}

TEST_CASE("a soldier with the nearest enemy just outside melee reach gets no target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(100.0f + kMeleeReach + 0.5f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("a soldier surrounded by friendlies only gets no target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(95.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(100.0f, 105.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("a soldier on cooldown gets no target even with an enemy in reach") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    s.attackCooldown[0] = kMeleeCooldown;
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("of two equidistant enemies the lower soldier index wins the tie") {
    // This is the cross-platform tie-break guard: strictly-less in
    // selectMeleeTarget keeps the first equidistant candidate, and
    // queryNeighbors walks insertion-ordered vectors, so the winner must be
    // index 1 (inserted before index 2) on every thread count and platform.
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);        // 0: seeker
    s.spawn(100.0f, 105.0f, 0, 0, Team::B, UnitType::Infantry, 1);        // 1: enemy, dist 5
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);        // 2: enemy, dist 5
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == 1);
}

TEST_CASE("melee resolution wired through a full tick draws blood") {
    // selectMeleeTarget and applyMeleeIntents both run inside Simulation::tick,
    // and applyMeleeIntents clears every intentTarget (single-use, see
    // Combat.cpp) before tick() returns. That means no intentTarget value
    // ever survives past a completed tick for a black-box caller to read, so
    // this test cannot assert on intentTarget the way task 3's tests did
    // (those properties now belong to the direct selectMeleeTarget unit
    // tests above). What this test owns instead is the wiring: that
    // selection, resolution, and the phase order in Simulation::tick actually
    // connect end to end and draw blood.
    Simulation sim(1200, 800, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 900; ++i) sim.tick(1.0f / 60.0f);

    bool anyDamaged = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        if (sim.soldierHealth(i) < kUnitStats[(int)sim.soldierUnitType(i)].maxHealth) {
            anyDamaged = true;
            break;
        }
    }
    CHECK(anyDamaged);
}

namespace {
// Builds two soldiers on opposing teams, adjacent, both able to swing.
SoldierHot makeDuel() {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    return s;
}
} // namespace

TEST_CASE("a melee intent costs the target health") {
    SoldierHot s = makeDuel();
    const uint8_t before = s.health[1];
    s.intentTarget[0] = 1;

    applyMeleeIntents(s);

    CHECK(s.health[1] == before - kMeleeDamage);
    CHECK(s.attackCooldown[0] > 0.0f);
}

TEST_CASE("overkill is dropped rather than carried over") {
    // Two attackers, one target with 1 health left. The first kills it; the
    // second must find health == 0 and waste its swing. Without the guard the
    // second attack would underflow the uint8_t to 255.
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(101.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(102.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);
    s.health[2] = 1;
    s.intentTarget[0] = 2;
    s.intentTarget[1] = 2;

    applyMeleeIntents(s);

    CHECK(s.health[2] == 0);
}

TEST_CASE("a dead attacker does not swing") {
    SoldierHot s = makeDuel();
    s.health[0] = 0;
    s.state[0] = SoldierState::Dead;
    const uint8_t before = s.health[1];
    s.intentTarget[0] = 1;

    applyMeleeIntents(s);

    CHECK(s.health[1] == before);
}

TEST_CASE("an out of range index is ignored rather than read") {
    SoldierHot s = makeDuel();
    s.intentTarget[0] = 999;
    applyMeleeIntents(s);  // must not read past the end
    CHECK(s.health[1] == kUnitStats[(int)UnitType::Infantry].maxHealth);
}
