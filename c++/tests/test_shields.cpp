#include <doctest/doctest.h>
#include "Shields.hpp"
#include "Loadout.hpp"
#include "Formation.hpp"
#include "Simulation.hpp"
#include "Projectiles.hpp"

TEST_CASE("a blow into the face is a front arc, into the back a rear arc") {
    // A squad facing +x. A blow travelling +x arrives from behind it; one
    // travelling -x arrives head on.
    CHECK(impactArc(-1.0f, 0.0f, 1.0f, 0.0f) == ImpactArc::Front);
    CHECK(impactArc( 1.0f, 0.0f, 1.0f, 0.0f) == ImpactArc::Rear);
    CHECK(impactArc( 0.0f, 1.0f, 1.0f, 0.0f) == ImpactArc::Side);
    CHECK(impactArc( 0.0f,-1.0f, 1.0f, 0.0f) == ImpactArc::Side);
}

TEST_CASE("a degenerate impact direction does not produce a nonsense arc") {
    CHECK(impactArc(0.0f, 0.0f, 1.0f, 0.0f) == ImpactArc::Front);
}

TEST_CASE("no shield blocks nothing in any arc or formation") {
    for (uint32_t s = 0; s < kFormationShapeCount; ++s) {
        for (uint32_t a = 0; a < 3; ++a) {
            // Formation cover is a bonus ON a shield, never a substitute for
            // one: a testudo of men carrying nothing blocks nothing.
            CHECK(blockChancePct(ShieldClass::None, (FormationShape)s,
                                 (ImpactArc)a, false) == 0);
        }
    }
}

TEST_CASE("a shield protects the front better than the rear") {
    const uint8_t front = blockChancePct(ShieldClass::Tower, FormationShape::Line,
                                         ImpactArc::Front, false);
    const uint8_t side  = blockChancePct(ShieldClass::Tower, FormationShape::Line,
                                         ImpactArc::Side, false);
    const uint8_t rear  = blockChancePct(ShieldClass::Tower, FormationShape::Line,
                                         ImpactArc::Rear, false);
    CHECK(front > side);
    CHECK(side > rear);
    CHECK(rear == 0);
}

TEST_CASE("a bigger shield covers more than a smaller one") {
    CHECK(blockChancePct(ShieldClass::Tower, FormationShape::Line, ImpactArc::Front, false)
          > blockChancePct(ShieldClass::Round, FormationShape::Line, ImpactArc::Front, false));
    CHECK(blockChancePct(ShieldClass::Round, FormationShape::Line, ImpactArc::Front, false)
          > blockChancePct(ShieldClass::Buckler, FormationShape::Line, ImpactArc::Front, false));
}

TEST_CASE("a testudo is far better covered than a line, in every arc") {
    for (uint32_t a = 0; a < 3; ++a) {
        CHECK(blockChancePct(ShieldClass::Tower, FormationShape::Testudo, (ImpactArc)a, false)
              > blockChancePct(ShieldClass::Tower, FormationShape::Line, (ImpactArc)a, false));
    }
}

TEST_CASE("no combination of shield and formation exceeds the block ceiling") {
    for (uint32_t sh = 0; sh < kShieldCount; ++sh) {
        for (uint32_t f = 0; f < kFormationShapeCount; ++f) {
            for (uint32_t a = 0; a < 3; ++a) {
                CHECK(blockChancePct((ShieldClass)sh, (FormationShape)f,
                                     (ImpactArc)a, false) <= kMaxBlockPct);
                CHECK(blockChancePct((ShieldClass)sh, (FormationShape)f,
                                     (ImpactArc)a, true) <= kMaxBlockPct);
            }
        }
    }
}

TEST_CASE("a shield helps less against a man than against an arrow") {
    CHECK(blockChancePct(ShieldClass::Round, FormationShape::Shieldwall, ImpactArc::Front, true)
          < blockChancePct(ShieldClass::Round, FormationShape::Shieldwall, ImpactArc::Front, false));
}

TEST_CASE("a mob is worse covered than a line carrying the same shields") {
    CHECK(blockChancePct(ShieldClass::Round, FormationShape::Mob, ImpactArc::Front, false)
          < blockChancePct(ShieldClass::Round, FormationShape::Line, ImpactArc::Front, false));
}

namespace {
// Eight shielded men in one squad, facing +x.
struct Volley {
    SoldierHot s;
    SquadHot q;
};

Volley shieldedSquad(FormationShape shape) {
    Volley v;
    v.q.spawn(Team::B, UnitType::Infantry);
    v.q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    v.q.shape[0] = (uint8_t)shape;
    v.q.facingX[0] = 1.0f;
    v.q.facingY[0] = 0.0f;
    v.q.memberCount[0] = 8;
    for (uint32_t k = 0; k < 8; ++k) {
        v.s.spawn(100.0f + (float)k, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 0);
    }
    return v;
}

// Fires an identical stream of arrows travelling in `dirX` at all eight men and
// returns how many of them died.
uint32_t casualtiesFrom(FormationShape shape, float dirX, uint32_t ticks) {
    Volley v = shieldedSquad(shape);
    for (uint32_t tick = 0; tick < ticks; ++tick) {
        ProjectileHot p;
        for (uint32_t k = 0; k < 8; ++k) {
            p.spawn(100.0f, 100.0f, dirX, 0.0f, Team::A, kArrowDamage,
                    kArrowLifetime, 0.0f, (uint8_t)WeaponClass::Bow);
            p.intentHitTarget[k] = k;
        }
        applyProjectileHits(p, v.s, v.q, Rng{ 42u, tick });
    }
    uint32_t dead = 0;
    for (uint32_t k = 0; k < 8; ++k) if (v.s.health[k] == 0) ++dead;
    return dead;
}
} // namespace

TEST_CASE("a volley into the rear kills more than the same volley into the face") {
    // dirX = +1 travels the way the squad faces, so it arrives from behind and
    // meets no shield at all. dirX = -1 arrives head on into a shieldwall.
    const uint32_t fromBehind = casualtiesFrom(FormationShape::Shieldwall,  1.0f, 40);
    const uint32_t fromAhead  = casualtiesFrom(FormationShape::Shieldwall, -1.0f, 40);
    CHECK(fromBehind > fromAhead);
}

TEST_CASE("a testudo under fire loses fewer men than the same squad in line") {
    const uint32_t inTestudo = casualtiesFrom(FormationShape::Testudo, -1.0f, 60);
    const uint32_t inLine    = casualtiesFrom(FormationShape::Line,    -1.0f, 60);
    CHECK(inTestudo < inLine);
}

TEST_CASE("a squad mid-drill has the worse cover of the two shapes") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::B, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    q.facingX[0] = 1.0f;
    q.facingY[0] = 0.0f;
    q.memberCount[0] = 1;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 0);

    // Settled in testudo: the best cover in the game.
    q.shape[0] = (uint8_t)FormationShape::Testudo;
    q.shapeBlend[0] = 0.0f;
    const uint8_t settled = shieldBlockPct(s, q, 0, -1.0f, 0.0f, false);

    // Mid-drill out of a line and into that testudo: it does not get the
    // testudo's cover until it has finished forming one.
    q.prevShape[0] = (uint8_t)FormationShape::Line;
    q.shapeBlend[0] = 1.0f;
    const uint8_t drilling = shieldBlockPct(s, q, 0, -1.0f, 0.0f, false);

    CHECK(drilling < settled);
    CHECK(drilling == blockChancePct(ShieldClass::Tower, FormationShape::Line,
                                     ImpactArc::Front, false));
}

TEST_CASE("an unshielded man in a squad blocks nothing whatever it is doing") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::B, UnitType::Archer);
    q.troopClass[0] = (uint8_t)TroopClass::Archer;   // no shield
    q.shape[0] = (uint8_t)FormationShape::Testudo;
    q.facingX[0] = 1.0f;
    q.facingY[0] = 0.0f;
    q.memberCount[0] = 1;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, TroopClass::Archer, 0);

    CHECK(shieldBlockPct(s, q, 0, -1.0f, 0.0f, false) == 0);
}
