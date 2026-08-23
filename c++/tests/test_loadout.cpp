#include <doctest/doctest.h>
#include "Loadout.hpp"
#include "Simulation.hpp"

TEST_CASE("every troop preset has a loadout row") {
    for (uint32_t t = 0; t < kTroopCount; ++t) {
        const Loadout& l = loadoutOf((uint8_t)t);
        CHECK(l.maxHealth > 0);
        CHECK(l.discipline > 0.0f);
        CHECK(l.discipline <= 1.0f);
        CHECK((uint32_t)l.weapon < kWeaponCount);
        CHECK((uint32_t)l.sidearm < kWeaponCount);
        CHECK((uint32_t)l.armor < kArmorCount);
        CHECK((uint32_t)l.shield < kShieldCount);
    }
}

TEST_CASE("only the legionary carries a sidearm different from his weapon") {
    for (uint32_t t = 0; t < kTroopCount; ++t) {
        const Loadout& l = loadoutOf((uint8_t)t);
        if ((TroopClass)t == TroopClass::Legionary) {
            CHECK(l.weapon == WeaponClass::Javelin);
            CHECK(l.sidearm == WeaponClass::Sword);
        }
    }
}

TEST_CASE("a spawned soldier's unit type matches its troop class") {
    Simulation sim(1200, 800, 42u, 0u);
    sim.init(400);
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const Loadout& l = loadoutOf(sim.soldierTroopClass(i));
        CHECK(l.unit == sim.soldierUnitType(i));
    }
}

TEST_CASE("a squad's discipline comes from its troop class") {
    Simulation sim(1200, 800, 42u, 0u);
    sim.init(400);
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const Loadout& l = loadoutOf(sim.squadTroopClass(s));
        CHECK(sim.squadDiscipline(s) == doctest::Approx(l.discipline));
    }
}
