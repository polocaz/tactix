#include <doctest/doctest.h>
#include "Loadout.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Combat.hpp"
#include "Soldiers.hpp"
#include "Rng.hpp"

namespace {
// Two one-man squads facing each other, both in Line. Melee resolution reads
// the squad tier for the attacker's formation, so even a two-soldier test needs
// one.
SquadHot duelSquads() {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.spawn(Team::B, UnitType::Infantry);
    for (size_t s = 0; s < 2; ++s) {
        q.shape[s] = (uint8_t)FormationShape::Line;
        q.memberCount[s] = 1;
    }
    q.facingX[0] =  1.0f; q.facingY[0] = 0.0f;
    q.facingX[1] = -1.0f; q.facingY[1] = 0.0f;   // they face each other
    return q;
}
} // namespace

TEST_CASE("more armor never raises the chance of being wounded") {
    for (uint32_t w = 0; w < kWeaponCount; ++w) {
        for (uint32_t a = 1; a < kArmorCount; ++a) {
            CHECK(kWoundChancePct[w][a] <= kWoundChancePct[w][a - 1]);
        }
    }
}

TEST_CASE("a mailed target takes more blows to kill than a padded one") {
    // Levy and Hoplite both have three health, so the ONLY thing that differs
    // here is what they are wearing. Comparing against a two-health troop would
    // measure health and armor together and prove neither.
    auto blowsToKill = [](TroopClass defender) {
        SoldierHot s;
        SquadHot q = duelSquads();
        s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Huscarl, 0);
        s.spawn(105.0f, 100.0f, 0, 0, Team::B, defender, 1);
        uint32_t blows = 0;
        for (uint32_t tick = 0; tick < 4000 && s.health[1] > 0; ++tick) {
            const Rng rng{ 42u, tick };
            s.intentTarget[0] = 1;
            s.attackCooldown[0] = 0.0f;
            applyMeleeIntents(s, q, rng);
            ++blows;
        }
        return blows;
    };
    REQUIRE(loadoutOf(TroopClass::Levy).maxHealth
            == loadoutOf(TroopClass::Hoplite).maxHealth);
    CHECK(blowsToKill(TroopClass::Hoplite) > blowsToKill(TroopClass::Levy));
}

TEST_CASE("a blow that fails to wound still consumes the attacker's cooldown") {
    SoldierHot s;
    SquadHot q = duelSquads();
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Archer, 0);   // sidearm sword
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, TroopClass::Knight, 1);   // plate, 20 percent
    // Find a tick whose roll fails, then assert the cooldown was still set.
    for (uint32_t tick = 0; tick < 200; ++tick) {
        const Rng rng{ 42u, tick };
        const uint8_t before = s.health[1];
        s.intentTarget[0] = 1;
        s.attackCooldown[0] = 0.0f;
        applyMeleeIntents(s, q, rng);
        if (s.health[1] == before) {
            CHECK(s.attackCooldown[0] > 0.0f);
            return;
        }
        s.health[1] = loadoutOf(TroopClass::Knight).maxHealth;  // reset and retry
    }
    FAIL("no failed wound roll in 200 attempts, which the 20 percent row makes implausible");
}

TEST_CASE("armor slows a man down") {
    CHECK(kArmorSpeedScale[(int)ArmorClass::Plate] < kArmorSpeedScale[(int)ArmorClass::Mail]);
    CHECK(kArmorSpeedScale[(int)ArmorClass::Mail] < kArmorSpeedScale[(int)ArmorClass::Padded]);
    CHECK(kArmorSpeedScale[(int)ArmorClass::None] == doctest::Approx(1.0f));
}

TEST_CASE("a plated soldier covers less ground than an unarmored one") {
    // Through steerToward rather than the table, so this fails if the scale is
    // defined but never applied.
    auto distanceIn = [](TroopClass tc, uint32_t ticks) {
        SoldierHot s;
        s.spawn(0.0f, 0.0f, 0, 0, Team::A, tc, 0);
        for (uint32_t t = 0; t < ticks; ++t) {
            steerToward(s, 0, Vec2{ 10000.0f, 0.0f }, kFixedTimestep);
            s.posX[0] += s.velX[0] * kFixedTimestep;
            s.posY[0] += s.velY[0] * kFixedTimestep;
        }
        return s.posX[0];
    };
    // Knight and Skirmisher have different base speeds, so compare troops that
    // share a UnitType: Hoplite (mail) against Skirmisher would not do either.
    // Levy is padded infantry and Huscarl is mailed infantry, same base speed.
    CHECK(distanceIn(TroopClass::Huscarl, 300) < distanceIn(TroopClass::Levy, 300));
}

TEST_CASE("a testudo swings more slowly than a line") {
    // Men under their shields fight badly, and that is the price of the cover.
    // Without it a testudo would be a free win in melee as well as against
    // arrows.
    auto cooldownAfterBlow = [](FormationShape shape) {
        SoldierHot s;
        SquadHot q = duelSquads();
        q.shape[0] = (uint8_t)shape;
        s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
        s.spawn(105.0f, 100.0f, 0, 0, Team::B, TroopClass::Levy, 1);
        s.intentTarget[0] = 1;
        const Rng rng{ 42u, 1u };
        applyMeleeIntents(s, q, rng);
        return s.attackCooldown[0];
    };
    CHECK(cooldownAfterBlow(FormationShape::Testudo)
          > cooldownAfterBlow(FormationShape::Line));
}
