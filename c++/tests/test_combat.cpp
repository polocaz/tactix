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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);  // 0: seeker
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);  // 1: friendly, in reach
    s.spawn(108.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);  // 2: enemy, in reach
    s.spawn(300.0f, 300.0f, 0, 0, Team::B, TroopClass::Legionary, 1);  // 3: enemy, out of reach
    s.spawn(400.0f, 400.0f, 0, 0, Team::A, TroopClass::Legionary, 0);  // 4: friendly, out of reach
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(108.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
    s.spawn(300.0f, 300.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
    s.spawn(400.0f, 400.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(100.0f + kMeleeReach - 0.1f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == 1);
}

TEST_CASE("a soldier with the nearest enemy just outside melee reach gets no target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(100.0f + kMeleeReach + 0.5f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("a soldier surrounded by friendlies only gets no target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(95.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(100.0f, 105.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;

    selectMeleeTarget(s, hash, 0, scratch);

    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("a soldier on cooldown gets no target even with an enemy in reach") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);        // 0: seeker
    s.spawn(100.0f, 105.0f, 0, 0, Team::B, TroopClass::Legionary, 1);        // 1: enemy, dist 5
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);        // 2: enemy, dist 5
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
    // 1800 ticks, raised from 900 when non-penetration landed. Bodies now
    // physically resist each other, which slowed the armies' approach by about
    // a third, and 900 ticks no longer reaches contact at all on this field.
    //
    // Raising a budget to keep a test green is worth being suspicious of, so
    // to be explicit: this asserts the WIRING (selection, resolution, and the
    // phase order connect end to end), not the timing. How long an army takes
    // to cross the field is a separate question, and a real one -- squads
    // advance at roughly a third of their nominal speed, which is a tuning
    // matter tracked for the tuning stage, not something this test should
    // silently encode.
    for (int i = 0; i < 1800; ++i) sim.tick(1.0f / 60.0f);

    bool anyDamaged = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        if (sim.soldierHealth(i) < loadoutOf(sim.soldierTroopClass(i)).maxHealth) {
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(105.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(101.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(102.0f, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 1);
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
    CHECK(s.health[1] == loadoutOf(TroopClass::Legionary).maxHealth);
}

TEST_CASE("a soldier reduced to zero health is marked dead and counted") {
    SoldierHot s = makeDuel();
    s.health[1] = 0;
    std::vector<uint32_t> casualties(2, 0u);
    std::vector<uint8_t> officerDied(2, 0u);

    recordCasualties(s, casualties, officerDied);

    CHECK(s.state[1] == SoldierState::Dead);
    CHECK(casualties[s.squadId[1]] == 1);
}

TEST_CASE("officer death is captured before compaction destroys the evidence") {
    // The officer is whoever holds slotIndex 0. Once compaction runs, that
    // soldier is gone and the next man has inherited the slot, so the flag has
    // to be set while the corpse still holds it.
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(112.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.slotIndex[0] = 0;   // officer
    s.slotIndex[1] = 1;
    s.health[0] = 0;

    std::vector<uint32_t> casualties(1, 0u);
    std::vector<uint8_t> officerDied(1, 0u);
    recordCasualties(s, casualties, officerDied);

    CHECK(officerDied[0] == 1);
}

TEST_CASE("a non officer death does not raise the officer flag") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.spawn(112.0f, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    s.slotIndex[0] = 0;
    s.slotIndex[1] = 1;
    s.health[1] = 0;

    std::vector<uint32_t> casualties(1, 0u);
    std::vector<uint8_t> officerDied(1, 0u);
    recordCasualties(s, casualties, officerDied);

    CHECK(officerDied[0] == 0);
    CHECK(casualties[0] == 1);
}

namespace {
// Every SoldierHot vector must stay exactly count long. Listing them
// explicitly is deliberate: when a field is added to SoldierHot, this
// list is the one place a compiler will not remind you to update, so
// it is written out rather than hidden behind a loop. prevPosX and
// prevPosY live on Simulation, not SoldierHot, so they are checked
// separately at the call site.
void checkArraysConsistent(const SoldierHot& s) {
    CHECK(s.posX.size() == s.count);
    CHECK(s.posY.size() == s.count);
    CHECK(s.velX.size() == s.count);
    CHECK(s.velY.size() == s.count);
    CHECK(s.dirX.size() == s.count);
    CHECK(s.dirY.size() == s.count);
    CHECK(s.team.size() == s.count);
    CHECK(s.unitType.size() == s.count);
    CHECK(s.state.size() == s.count);
    CHECK(s.squadId.size() == s.count);
    CHECK(s.slotIndex.size() == s.count);
    CHECK(s.health.size() == s.count);
    CHECK(s.attackCooldown.size() == s.count);
    CHECK(s.intentTarget.size() == s.count);
    CHECK(s.intentFire.size() == s.count);
}

// Gives soldier k a distinctive value in every one of the 15 SoldierHot
// fields plus prevPosX/prevPosY (all 17 arrays compactDead must move
// together), each one independently encoding the same original k. Matching
// vector lengths alone cannot catch a swap that moved 14 fields correctly
// and dropped the 15th, because the sizes still agree; this fingerprint is
// what actually defends the swap block, by making a single left-behind
// field detectable after the fact.
void spawnFingerprinted(SoldierHot& s, int k) {
    s.spawn(100.0f + (float)k, 200.0f + (float)k, 0.0f, 0.0f, Team::A, TroopClass::Legionary, 0);
    const size_t i = s.count - 1;
    s.velX[i]           = 300.0f + (float)k;
    s.velY[i]           = 400.0f + (float)k;
    s.dirX[i]           = 500.0f + (float)k;
    s.dirY[i]           = 600.0f + (float)k;
    s.team[i]           = (k % 2 == 0) ? Team::A : Team::B;
    s.unitType[i]        = (UnitType)(k % (int)kUnitTypeCount);
    s.state[i]           = SoldierState::Forming;
    s.squadId[i]         = (uint16_t)k;
    s.slotIndex[i]       = (uint16_t)k;
    s.health[i]          = (uint8_t)(k + 1);
    s.attackCooldown[i]  = 700.0f + (float)k;
    s.intentTarget[i]    = (uint32_t)(1000 + k);
    s.intentFire[i]      = (uint8_t)(k % 2);
}

// Decodes k from posX and asserts every other field, including prevPosX/
// prevPosY, still encodes that SAME k. Any field that moved independently of
// the rest (a swap line for one vector silently dropped, or applied to the
// wrong vector) shows up here as a mismatch even though every vector is
// still the right length.
void checkFingerprintIntact(const SoldierHot& s, size_t i,
                            const std::vector<float>& prevX,
                            const std::vector<float>& prevY) {
    const int k = (int)std::lround(s.posX[i] - 100.0f);
    CAPTURE(i);
    CAPTURE(k);
    CHECK(s.posY[i]          == doctest::Approx(200.0f + k));
    CHECK(s.velX[i]           == doctest::Approx(300.0f + k));
    CHECK(s.velY[i]           == doctest::Approx(400.0f + k));
    CHECK(s.dirX[i]           == doctest::Approx(500.0f + k));
    CHECK(s.dirY[i]           == doctest::Approx(600.0f + k));
    CHECK(s.team[i]           == ((k % 2 == 0) ? Team::A : Team::B));
    CHECK((int)s.unitType[i]  == k % (int)kUnitTypeCount);
    CHECK(s.state[i]          == SoldierState::Forming);
    CHECK(s.squadId[i]        == (uint16_t)k);
    CHECK(s.slotIndex[i]      == (uint16_t)k);
    CHECK(s.health[i]         == (uint8_t)(k + 1));
    CHECK(s.attackCooldown[i] == doctest::Approx(700.0f + k));
    CHECK(s.intentTarget[i]   == (uint32_t)(1000 + k));
    CHECK(s.intentFire[i]     == (uint8_t)(k % 2));
    CHECK(prevX[i]            == doctest::Approx(800.0f + k));
    CHECK(prevY[i]            == doctest::Approx(900.0f + k));
}
} // namespace

TEST_CASE("compaction removes the dead and keeps every array the same length") {
    SoldierHot s;
    for (int k = 0; k < 5; ++k) {
        s.spawn(100.0f + k, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    }
    std::vector<float> prevX(5, 0.0f), prevY(5, 0.0f);
    s.state[1] = SoldierState::Dead;
    s.state[3] = SoldierState::Dead;

    compactDead(s, prevX, prevY);

    CHECK(s.count == 3);
    checkArraysConsistent(s);
    CHECK(prevX.size() == 3);
    CHECK(prevY.size() == 3);
    for (size_t i = 0; i < s.count; ++i) {
        CHECK(s.state[i] != SoldierState::Dead);
    }
}

TEST_CASE("compacting an army with no dead changes nothing") {
    SoldierHot s;
    for (int k = 0; k < 4; ++k) {
        s.spawn(100.0f + k, 100.0f, 0, 0, Team::A, TroopClass::Legionary, 0);
    }
    std::vector<float> prevX(4, 0.0f), prevY(4, 0.0f);

    compactDead(s, prevX, prevY);

    CHECK(s.count == 4);
    checkArraysConsistent(s);
    CHECK(prevX.size() == 4);
    CHECK(prevY.size() == 4);
    CHECK(s.posX[3] == doctest::Approx(103.0f));
}

TEST_CASE("compaction moves every field together, not just lengths") {
    // Kills two non-adjacent, non-tail soldiers so the swap-with-back loop
    // has to pull survivors in from the back more than once. The lengths
    // alone (previous test) cannot tell a correct swap from one that left a
    // field behind; the fingerprint can.
    SoldierHot s;
    std::vector<float> prevX, prevY;
    const int N = 6;
    for (int k = 0; k < N; ++k) {
        spawnFingerprinted(s, k);
        prevX.push_back(800.0f + (float)k);
        prevY.push_back(900.0f + (float)k);
    }
    s.state[1] = SoldierState::Dead;
    s.state[3] = SoldierState::Dead;

    compactDead(s, prevX, prevY);

    CHECK(s.count == (size_t)(N - 2));
    checkArraysConsistent(s);
    CHECK(prevX.size() == s.count);
    CHECK(prevY.size() == s.count);
    for (size_t i = 0; i < s.count; ++i) {
        checkFingerprintIntact(s, i, prevX, prevY);
    }
}

TEST_CASE("compaction handles the dead soldier being the last element") {
    // i reaches the last slot with last == i, taking the "already at the
    // back, just pop" branch rather than the swap branch. Easy to fencepost.
    SoldierHot s;
    std::vector<float> prevX, prevY;
    const int N = 5;
    for (int k = 0; k < N; ++k) {
        spawnFingerprinted(s, k);
        prevX.push_back(800.0f + (float)k);
        prevY.push_back(900.0f + (float)k);
    }
    s.state[N - 1] = SoldierState::Dead;

    compactDead(s, prevX, prevY);

    CHECK(s.count == (size_t)(N - 1));
    checkArraysConsistent(s);
    CHECK(prevX.size() == s.count);
    CHECK(prevY.size() == s.count);
    for (size_t i = 0; i < s.count; ++i) {
        checkFingerprintIntact(s, i, prevX, prevY);
    }
}

TEST_CASE("compaction handles two adjacent dead soldiers at the end") {
    // Soldier N-2 dies and gets swapped with N-1 -- which is ALSO dead. If
    // the loop advanced i after that swap it would miss re-examining the
    // freshly swapped-in corpse. Both tail soldiers must still be gone and
    // every survivor still intact.
    SoldierHot s;
    std::vector<float> prevX, prevY;
    const int N = 6;
    for (int k = 0; k < N; ++k) {
        spawnFingerprinted(s, k);
        prevX.push_back(800.0f + (float)k);
        prevY.push_back(900.0f + (float)k);
    }
    s.state[N - 1] = SoldierState::Dead;
    s.state[N - 2] = SoldierState::Dead;

    compactDead(s, prevX, prevY);

    CHECK(s.count == (size_t)(N - 2));
    checkArraysConsistent(s);
    CHECK(prevX.size() == s.count);
    CHECK(prevY.size() == s.count);
    for (size_t i = 0; i < s.count; ++i) {
        checkFingerprintIntact(s, i, prevX, prevY);
    }
}

TEST_CASE("soldiers actually die in a running battle") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    const size_t before = sim.getAgentCount();
    for (int i = 0; i < 1800; ++i) sim.tick(1.0f / 60.0f);
    CHECK(sim.getAgentCount() < before);
}

// The two properties formerly tested here -- long-range acquisition beyond
// an individual soldier's own sight, and never acquiring a same-team target
// -- used to be checked by running a full 500-soldier, 1400-tick battle and
// reading Simulation::squadTargetSoldier() afterward. That accessor no
// longer works for this: phaseResolution now clears squads.targetSoldier to
// UINT32_MAX at the end of every tick (see the comment there), because a
// value left over from before compaction is a stale index into the OLD
// soldier numbering, not a value with any meaning after the tick completes.
// A caller reading squadTargetSoldier() between ticks always sees
// UINT32_MAX now -- correct, but it means these two properties can no longer
// be observed through a live Simulation from outside a tick. Both are
// covered directly against selectTargetSoldier() instead, in test_squads.cpp
// ("selectTargetSoldier does not acquire a same-team target on a squad's
// very first decide" and "...can acquire a target beyond an individual
// soldier's own sight"), which exercise the exact same logic without relying
// on a window that no longer exists from the outside.

TEST_CASE("compactDead moves steadyTimer with the rest of the soldier") {
    // A soldier array that compaction does not move desyncs the structure of
    // arrays, and the corruption then shows up as a wrong value on an
    // unrelated agent, which is close to impossible to trace back. This is the
    // cheap guard against forgetting one.
    SoldierHot s;
    std::vector<float> prevX, prevY;
    s.spawn(0.0f, 0.0f, 0.0f, 0.0f, Team::A, TroopClass::Archer, 0);
    s.spawn(5.0f, 0.0f, 0.0f, 0.0f, Team::A, TroopClass::Archer, 0);
    prevX.assign(2, 0.0f);
    prevY.assign(2, 0.0f);

    s.state[0] = SoldierState::Dead;
    s.steadyTimer[1] = 1.25f;

    compactDead(s, prevX, prevY);
    REQUIRE(s.count == 1);
    CHECK(s.steadyTimer[0] == doctest::Approx(1.25f));
}
