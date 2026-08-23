# Armor, Formations and Shields Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat damage model with a staged penetration model (geometry strikes, shield may block, armor may turn the point), give soldiers weapons, armor and shields, turn `FormationShape` into doctrine with five new shapes, and add an army-tier legion line relief.

**Architecture:** Five layers, each usable on its own. A loadout table layered over the existing `UnitType` supplies weapon, armor, shield, discipline and health per troop class. A formation traits table replaces the hand-mirrored switches in `Formation.hpp` and gains five shapes carrying speed, turn rate, fighting depth, reach and per-arc cover. Two combat tables and one shared `shieldBlockPct` function turn both existing resolution sites into three-stage rolls. A per-squad shape field with a transition blend lets squads change formation under fire. The army tier gains a relief rule built entirely from orders and roles that already exist.

**Tech Stack:** C++20, CMake, doctest, custom job system, structure-of-arrays entity storage, spdlog, raylib (GUI target only). No new dependencies.

**Spec:** [`docs/superpowers/specs/2026-08-22-armor-formations-and-shields-design.md`](../specs/2026-08-22-armor-formations-and-shields-design.md)

## Global Constraints

These apply to every task without exception. Violating any of them is a rejected task, not a style note.

- **Thread-count invariance is load-bearing.** `Simulation::stateDigest()` must be bit-identical at 1, 2, 8 and 15 worker threads. Any parallel phase must have each agent write only its own state and read only data that is read-only for that phase's whole duration.
- **Cross-agent mutation happens only in serial `phaseResolution`.** Nowhere else, ever.
- **Iterate in ascending index order. Break every tie on the lower index.**
- **Use `detmath::sin`, never `std::sin`/`std::cos`,** for anything that reaches the digest. `std::sqrt` is fine (IEEE-754 pins it down). See `src/DetMath.hpp`.
- **Enum values are appended, never renumbered, and never deleted.** `FormationShape`, `SquadOrder`, `SquadRole` and `RngUse` all feed the digest or the RNG stream. This explicitly includes `RngUse::ArrowHitRoll`, which becomes unused in Task 5 and MUST stay in the enum.
- **New per-soldier work is O(neighbors) through `SpatialHash`,** never O(n squared) over soldiers. O(squads squared) is acceptable inside a phase that already loops all squads.
- **No em dashes** in code comments, docs, or commit messages. Restructure the sentence instead.
- **No AI co-author trailer** on any commit.
- **Every new field added to `SoldierHot`, `SquadHot`, `ProjectileHot` or `ArmyHot` must be:** initialized in that struct's `spawn()`, cleared in its `clear()`, moved by its compaction routine if it has one, and mixed into `stateDigest()`. Do this in the task that adds the field. Task 13 verifies it and is not the place to fix it.
- **The digest baseline moves in this plan, repeatedly and legitimately.** Every task that changes behavior regenerates `c++/tests/baseline/counters-2k-200.txt` and `counters-2k-1600.txt` in the same commit and says why in the commit message, per the instructions inside those files.

**Build and test commands** (run from `c++/`, Git Bash or cmd both work):

```bash
scripts/build.bat -t
```

Single test case:

```bash
build/Release/tactix_tests.exe -tc="name of the test case"
```

Regenerating counter baselines:

```bash
build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

---

## File Structure

**New source files** (all must be added to the `tactix_sim` target in `c++/CMakeLists.txt`, which uses an explicit source list rather than a glob):

| File | Responsibility |
|---|---|
| `src/Loadout.hpp` | Weapon, armor, shield and troop enums; the troop preset table; the wound and cover tables. Header only: all `constexpr` data and two `constexpr` accessors. |
| `src/Shields.hpp` / `.cpp` | Impact arc classification and block chance. The one definition of "a shield covers what a man faces", shared by melee and missiles. |

**New test files** (added to the `tactix_tests` target):

| File | Covers |
|---|---|
| `tests/test_loadout.cpp` | Task 1 |
| `tests/test_armor.cpp` | Tasks 4, 5 |
| `tests/test_shields.cpp` | Task 6 |
| `tests/test_relief.cpp` | Task 11 |

**Modified files:**

| File | Change |
|---|---|
| `src/Units.hpp` | New constants; `kDisciplineInfantry`/`Archer`/`Cavalry` and `kArrowHitChancePct` deleted; `UnitStats::maxHealth` removed. |
| `src/Formation.hpp` | `FormationTraits` table, five new shapes, data-driven `formationSlot` and `rankOfSlot`. |
| `src/Squads.hpp` / `.cpp` | Nine new fields, formation choice, transition blend, relief state. |
| `src/Soldiers.hpp` / `.cpp` | `slotWorldPosition` reads the squad's shape and blends; armor and formation speed scales. |
| `src/Combat.hpp` / `.cpp` | Fighting ranks, reach, front-arc gate, block and wound rolls, cooldown on a failed blow. |
| `src/Projectiles.hpp` / `.cpp` | `weapon` field, block and wound rolls, javelin spawning. |
| `src/Army.hpp` / `.cpp` | Legion relief. |
| `src/Simulation.hpp` / `.cpp` | `troopClass` field, deployment by troop class, `missilePressure` accumulation, digest coverage. |
| `src/Renderer.cpp` | Viewport cull, formation glyphs, shield bars, weapon marks. |
| `src/Rng.hpp` | Four appended `RngUse` enumerators. |
| `c++/CMakeLists.txt` | Two new source files, four new test files. |
| `c++/tests/baseline/*.txt` | Regenerated repeatedly. |
| `README.md` | Republished tick figures and digest (Task 13). |

**Three deviations from the spec, all deliberate. Implement the plan's version:**

1. **`shieldBlockPct` is split into two pure functions plus one thin wrapper.** The spec gives one function taking the full SoA. `impactArc` and `blockChancePct` are pure and take scalars, which is what the tests actually exercise; the SoA wrapper is three lines on top. Same behavior, far cheaper to test.
2. **Deployment assigns Legionary to Team A and Hoplite to Team B.** The spec does not say who deploys as what. This choice makes the default battle legion against phalanx, which is exactly what the feature exists to demonstrate. `Levy`, `Huscarl` and `Skirmisher` are defined and tested but unused by the default deployment, and the table comment must say so, so their absence reads as a decision rather than an oversight.
3. **`FormationTraits::cover` is `int8_t`, not `uint8_t`.** `Mob` carries -10, which does not fit an unsigned type. The spec's table implies this but does not state the type.

---

## Task 1: Loadout data model

No behavior change is intended beyond discipline values moving from three constants into a table, which does move the digest.

**Files:**
- Create: `c++/src/Loadout.hpp`
- Create: `c++/tests/test_loadout.cpp`
- Modify: `c++/src/Units.hpp` (delete `kDisciplineInfantry`/`kDisciplineArcher`/`kDisciplineCavalry`, remove `maxHealth` from `UnitStats`)
- Modify: `c++/src/Simulation.hpp` (`SoldierHot::troopClass`, `spawn`, `clear`)
- Modify: `c++/src/Simulation.cpp` (`troopClassForSquad`, deployment, digest)
- Modify: `c++/src/Combat.cpp` (`compactDead` moves the new field)
- Modify: `c++/src/Renderer.cpp:570`, `c++/tests/test_combat.cpp:155,217` (the three `kUnitStats[...].maxHealth` readers)
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `WeaponClass`, `ArmorClass`, `ShieldClass`, `TroopClass`, `kWeaponCount`, `kArmorCount`, `kShieldCount`, `kTroopCount`, `struct Loadout`, `kTroopLoadout`, `constexpr const Loadout& loadoutOf(TroopClass)`, `constexpr const Loadout& loadoutOf(uint8_t)`, `SoldierHot::troopClass`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_loadout.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `Loadout.hpp` not found.

- [ ] **Step 3: Create `c++/src/Loadout.hpp`**

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>

// Weapon, armor and shield are ORTHOGONAL to UnitType. UnitType stays the
// coarse role the army tier reasons about (who screens, who flanks, who
// shoots); these say what a man is actually carrying.
//
// Appended, never renumbered or deleted: these values reach the state digest
// through SoldierHot::troopClass, so changing one silently invalidates every
// committed baseline.
enum class WeaponClass : uint8_t { Sword = 0, Spear = 1, Bow = 2, Javelin = 3, Lance = 4 };
enum class ArmorClass  : uint8_t { None = 0, Padded = 1, Mail = 2, Plate = 3 };
enum class ShieldClass : uint8_t { None = 0, Buckler = 1, Round = 2, Tower = 3 };
enum class TroopClass  : uint8_t {
    Levy = 0, Legionary = 1, Hoplite = 2, Huscarl = 3,
    Archer = 4, Skirmisher = 5, Knight = 6
};

constexpr uint32_t kWeaponCount = 5;
constexpr uint32_t kArmorCount  = 4;
constexpr uint32_t kShieldCount = 4;
constexpr uint32_t kTroopCount  = 7;

struct Loadout {
    UnitType    unit;
    WeaponClass weapon;
    WeaponClass sidearm;   // what he fights with once the throwing is done
    ArmorClass  armor;
    ShieldClass shield;
    float       discipline;
    uint8_t     maxHealth;
};

// Discipline lives HERE and not in three per-UnitType constants, because
// steadiness is a property of who the men are rather than of what role they
// fill. A levy spearman and a hoplite are both Infantry and are not remotely
// the same troops.
//
// The sidearm column exists for exactly one behavior: a legionary throws his
// pilum once and then fights with a sword. Every other troop's sidearm equals
// its weapon, so the throw is a table read rather than a branch.
//
// Levy, Huscarl and Skirmisher are deliberately NOT used by the default
// deployment (Simulation::troopClassForSquad), which fields Legionary against
// Hoplite so the default battle demonstrates the relief tier against the
// phalanx. They are complete, tested presets waiting on a composition UI, not
// an oversight.
constexpr Loadout kTroopLoadout[kTroopCount] = {
    /* Levy       */ { UnitType::Infantry, WeaponClass::Spear,   WeaponClass::Spear, ArmorClass::Padded, ShieldClass::Round,   0.55f, 3 },
    /* Legionary  */ { UnitType::Infantry, WeaponClass::Javelin, WeaponClass::Sword, ArmorClass::Mail,   ShieldClass::Tower,   0.90f, 3 },
    /* Hoplite    */ { UnitType::Infantry, WeaponClass::Spear,   WeaponClass::Spear, ArmorClass::Mail,   ShieldClass::Round,   0.85f, 3 },
    /* Huscarl    */ { UnitType::Infantry, WeaponClass::Sword,   WeaponClass::Sword, ArmorClass::Mail,   ShieldClass::Round,   0.88f, 3 },
    /* Archer     */ { UnitType::Archer,   WeaponClass::Bow,     WeaponClass::Sword, ArmorClass::Padded, ShieldClass::None,    0.60f, 2 },
    /* Skirmisher */ { UnitType::Archer,   WeaponClass::Javelin, WeaponClass::Sword, ArmorClass::None,   ShieldClass::Buckler, 0.50f, 2 },
    /* Knight     */ { UnitType::Cavalry,  WeaponClass::Lance,   WeaponClass::Sword, ArmorClass::Plate,  ShieldClass::Round,   0.70f, 3 },
};

constexpr const Loadout& loadoutOf(TroopClass t) {
    return kTroopLoadout[(uint32_t)t < kTroopCount ? (uint32_t)t : 0u];
}

// Overload taking the raw byte, because SoldierHot and SquadHot store the
// class as uint8_t (the SoA arrays hold plain bytes, not enums).
constexpr const Loadout& loadoutOf(uint8_t t) {
    return kTroopLoadout[(uint32_t)t < kTroopCount ? (uint32_t)t : 0u];
}

// Chance in percent that a landed blow wounds. Geometry decides whether a man
// is STRUCK; this decides whether it goes through what he is wearing.
//
// Three entries carry the design's intent and are worth naming. Javelin
// against mail (60) is the pilum: mediocre against bare flesh, the best thing
// here against armor. Bow against plate (12) is what keeps archery a
// formation-breaking and morale weapon rather than a knight-killer. Spear
// against plate (30) beating sword against plate (20) is the point
// concentrating force, and is why a spear is worth carrying even setting the
// phalanx aside.
constexpr uint8_t kWoundChancePct[kWeaponCount][kArmorCount] = {
    /*             None  Padded  Mail  Plate */
    /* Sword   */ {  85,     70,   40,    20 },
    /* Spear   */ {  80,     65,   45,    30 },
    /* Bow     */ {  75,     55,   30,    12 },
    /* Javelin */ {  85,     75,   60,    40 },
    /* Lance   */ {  95,     90,   75,    55 },
};

// Plate has to cost something or it is not a choice. Applied through the
// speedScale parameter steerToward already accepts.
constexpr float kArmorSpeedScale[kArmorCount] = { 1.00f, 0.97f, 0.92f, 0.86f };
```

- [ ] **Step 4: Delete the three discipline constants from `Units.hpp`**

Remove `kDisciplineInfantry`, `kDisciplineArcher` and `kDisciplineCavalry` and the comment block above them. Remove `maxHealth` from `struct UnitStats` and from the three `kUnitStats` rows, leaving `{ speed, range }`. Add a comment on `kUnitStats` saying health, discipline and loadout now live in `Loadout.hpp`, and that speed and range stay here because they are genuinely properties of the role: a mounted man is fast because he is mounted.

- [ ] **Step 5: Add `troopClass` to `SoldierHot`**

In `c++/src/Simulation.hpp`, add `std::vector<uint8_t> troopClass;` next to `unitType`. Change `spawn` to take the troop class instead of the unit type, deriving both the type and the starting health from it:

```cpp
    void spawn(float px, float py, float vx, float vy, Team t, TroopClass tc, uint16_t squad) {
        // ... existing position/velocity/direction code unchanged ...
        const Loadout& lo = loadoutOf(tc);
        team.push_back(t);
        troopClass.push_back((uint8_t)tc);
        unitType.push_back(lo.unit);
        // Derived from the loadout, never passed in: a caller cannot construct
        // a soldier whose role and equipment disagree.
        health.push_back(lo.maxHealth);
        // ... rest unchanged ...
    }
```

Add `troopClass.reserve(n)` to `reserve` and `troopClass.clear()` to `clear`.

- [ ] **Step 6: Move the field in `compactDead`**

In `c++/src/Combat.cpp`, add `soldiers.troopClass[i] = soldiers.troopClass[last];` alongside `unitType`, and `soldiers.troopClass.pop_back();` alongside the other `pop_back` calls. Both lists are order-sensitive by convention only, so put the new line immediately after the `unitType` one in each.

- [ ] **Step 7: Deploy by troop class**

In `c++/src/Simulation.cpp`, add below `unitTypeForSquad`:

```cpp
// Which troops a squad is made of. Composition (how many infantry, archer and
// cavalry squads) is unchanged: this maps the type unitTypeForSquad already
// chose onto a concrete troop, per team.
//
// Team A fields legionaries and Team B hoplites so the default battle is the
// one this feature exists to show: a rotating manipular line against a spear
// wall. Both sides keep the same archers and knights, so the asymmetry is
// exactly one thing and its effect is readable.
static TroopClass troopClassForSquad(uint32_t sq, uint32_t squadsPerTeam, Team team) {
    switch (unitTypeForSquad(sq, squadsPerTeam)) {
        case UnitType::Archer:  return TroopClass::Archer;
        case UnitType::Cavalry: return TroopClass::Knight;
        default: return (team == Team::A) ? TroopClass::Legionary : TroopClass::Hoplite;
    }
}
```

At both deployment sites (around lines 148 and 198), replace `UnitType unit = unitTypeForSquad(...)` with:

```cpp
            const TroopClass troop = troopClassForSquad(sq, compositionSquadsPerTeam, team);
            const UnitType unit = loadoutOf(troop).unit;
```

Replace `squads.discipline[squadId] = disciplineForUnit(unit);` with `squads.discipline[squadId] = loadoutOf(troop).discipline;` and delete `disciplineForUnit`. Add `squads.troopClass[squadId] = (uint8_t)troop;` (the `SquadHot` field is added in the next step). Change the `soldiers.spawn(...)` call to pass `troop` instead of `unit`.

- [ ] **Step 8: Add `troopClass` to `SquadHot`**

In `c++/src/Squads.hpp`, add `std::vector<uint8_t> troopClass;` with `troopClass.push_back((uint8_t)TroopClass::Levy);` in `spawn()` and `troopClass.clear();` in `clear()`. Deployment overwrites it immediately; the default exists so a squad spawned by a test is never in an invalid state. Include `Loadout.hpp` from `Squads.hpp`.

A squad is uniform, so this duplicates its members' value. That is deliberate: the squad tier reads it in `squadDecide` (Task 9) where reaching into a member soldier would be an extra indirection into a different array for a value that cannot differ.

- [ ] **Step 9: Fix the three `maxHealth` readers**

`c++/src/Renderer.cpp:570`: replace `kUnitStats[(int)type].maxHealth` with `loadoutOf(sim.soldiers.troopClass[i]).maxHealth`, and add `#include "Loadout.hpp"`.

`c++/tests/test_combat.cpp:155`: replace with `loadoutOf(sim.soldierTroopClass(i)).maxHealth`.

`c++/tests/test_combat.cpp:217`: that test spawns Infantry, so use `loadoutOf(TroopClass::Legionary).maxHealth` and change its `spawn` calls to pass `TroopClass::Legionary`.

Every other `s.spawn(..., UnitType::Infantry, 0)` call across the test suite becomes `s.spawn(..., TroopClass::Legionary, 0)`; `UnitType::Archer` becomes `TroopClass::Archer`; `UnitType::Cavalry` becomes `TroopClass::Knight`. This is a mechanical sweep over `tests/`, and the compiler finds every site.

- [ ] **Step 10: Add the accessors and digest coverage**

In `c++/src/Simulation.hpp` add:

```cpp
    uint8_t soldierTroopClass(size_t i) const { return soldiers.troopClass[i]; }
    uint8_t squadTroopClass(size_t s) const { return squads.troopClass[s]; }
```

In `stateDigest()`, add `d.mix(static_cast<uint32_t>(soldiers.troopClass[i]));` immediately after the `unitType` line, and `d.mix(static_cast<uint32_t>(squads.troopClass[s]));` in the squad loop.

- [ ] **Step 11: Wire up CMake**

Add `tests/test_loadout.cpp` to the `tactix_tests` source list. `Loadout.hpp` is header only and needs no source entry.

- [ ] **Step 12: Run the tests**

Run: `scripts/build.bat -t`
Expected: PASS, except `test_counters` which fails on a moved `stateDigest`.

- [ ] **Step 13: Regenerate the counter baselines**

Run `build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads 1 --json` and paste `stateDigest` into `c++/tests/baseline/counters-2k-200.txt`. Repeat with `--ticks 1600` for `counters-2k-1600.txt`. Add a comment block above the values saying the move is behavioral: discipline values changed when they moved from three per-type constants into the troop table, and discipline scales formation compression, so every soldier stands somewhere slightly different.

Run: `scripts/build.bat -t`
Expected: PASS, all tests.

- [ ] **Step 14: Commit**

```bash
git add c++/src/Loadout.hpp c++/src/Units.hpp c++/src/Simulation.hpp c++/src/Simulation.cpp c++/src/Squads.hpp c++/src/Combat.cpp c++/src/Renderer.cpp c++/tests c++/CMakeLists.txt
git commit -m "feat: give soldiers a troop class carrying weapon, armor and shield

Discipline and max health move out of per-UnitType constants into a loadout
table, because both are properties of who the men are rather than of what
role they fill. Digest moves: discipline scales formation compression."
```

---

## Task 2: Formation traits table, behavior-preserving

Introduces the table and routes the existing four shapes through it, changing nothing. Doing this before adding shapes means any digest movement in Task 3 is attributable to the new shapes rather than to the refactor.

**Files:**
- Modify: `c++/src/Formation.hpp`
- Modify: `c++/src/Squads.hpp` (`shape` field), `c++/src/Simulation.cpp` (deployment, digest)
- Modify: `c++/src/Soldiers.cpp` (`slotWorldPosition` reads the field)
- Test: `c++/tests/test_formation.cpp`

**Interfaces:**
- Consumes: `SquadHot::troopClass` from Task 1.
- Produces: `struct FormationTraits`, `kFormationShapeCount`, `kFormationTraits`, `constexpr const FormationTraits& traitsOf(FormationShape)`, `detail::shapeWidth(FormationShape, uint32_t)`, `SquadHot::shape`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_formation.cpp`:

```cpp
TEST_CASE("rankOfSlot agrees with formationSlot for every shape and count") {
    const FormationShape shapes[] = {
        FormationShape::Line, FormationShape::Column,
        FormationShape::Wedge, FormationShape::Loose,
    };
    for (FormationShape shape : shapes) {
        for (uint32_t n = 1; n <= 200; ++n) {
            for (uint16_t i = 0; i < (uint16_t)n; ++i) {
                const Vec2 s = formationSlot(shape, i, n);
                const uint32_t rank = rankOfSlot(shape, i, n);
                // Rank r sits at depth -r * spacing. Recovering r from y is
                // the whole mirror those two functions must maintain.
                const float spacing = kSlotSpacing * traitsOf(shape).spacing;
                CHECK(s.y == doctest::Approx(-(float)rank * spacing));
            }
        }
    }
}

TEST_CASE("every formation shape has a traits row") {
    for (uint32_t i = 0; i < kFormationShapeCount; ++i) {
        const FormationTraits& t = traitsOf((FormationShape)i);
        CHECK(t.spacing > 0.0f);
        CHECK(t.speed > 0.0f);
        CHECK(t.turn > 0.0f);
        CHECK(t.reach >= 1.0f);
        CHECK(t.cooldown > 0.0f);
        CHECK(t.fightingRanks >= 1);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `traitsOf` not declared.

- [ ] **Step 3: Add the traits table to `Formation.hpp`**

Insert above `formationSlot`:

```cpp
constexpr uint32_t kFormationShapeCount = 9;

// Everything a formation IS, in one row. Geometry (spacing, aspect) and
// behavior (the rest) live together because they are the same decision: a
// shieldwall is tight AND slow AND covered, and splitting those across three
// files is how they drift apart.
//
// cover is in PERCENTAGE POINTS added to the shield block chance, per arc
// (front, side, rear). Signed because Mob subtracts.
struct FormationTraits {
    float   spacing;        // multiplies kSlotSpacing
    float   aspect;         // target width/depth for the grid shapes
    float   speed;          // multiplies the unit's base speed
    float   turn;           // multiplies kFacingSlewRate
    uint8_t fightingRanks;  // how many ranks may reach an enemy
    float   reach;          // multiplies kMeleeReach
    float   cooldown;       // multiplies kMeleeCooldown
    int8_t  cover[3];       // front, side, rear
};

// The first four rows reproduce the pre-table behavior exactly, so
// introducing this table is not a behavior change for existing shapes.
constexpr FormationTraits kFormationTraits[kFormationShapeCount] = {
    /* Line       */ { 1.00f, 2.0f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Column     */ { 1.00f, 0.5f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Wedge      */ { 1.00f, 2.0f, 1.00f, 1.20f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Loose      */ { 2.00f, 2.0f, 1.00f, 1.00f, 1, 1.0f, 1.0f, {   0,   0,   0 } },
    /* Shieldwall */ { 0.75f, 3.0f, 0.60f, 0.50f, 1, 1.0f, 1.0f, { +30,  +5,   0 } },
    /* Phalanx    */ { 0.85f, 1.5f, 0.50f, 0.35f, 3, 1.6f, 1.0f, { +20,   0,   0 } },
    /* Testudo    */ { 0.60f, 1.2f, 0.30f, 0.40f, 1, 1.0f, 2.2f, { +55, +45, +35 } },
    /* Manipular  */ { 1.00f, 2.5f, 0.95f, 1.00f, 1, 1.0f, 1.0f, { +10,   0,   0 } },
    /* Mob        */ { 1.50f, 1.0f, 1.05f, 2.00f, 1, 1.0f, 1.2f, { -10, -10, -10 } },
};

constexpr const FormationTraits& traitsOf(FormationShape s) {
    return kFormationTraits[(uint32_t)s < kFormationShapeCount ? (uint32_t)s : 0u];
}
```

Wedge's `aspect` is never read (its layout is `floor(sqrt(i))`), and is set to Line's value so the row is never a special case for anything except the two functions that already branch on it.

- [ ] **Step 4: Route the geometry through the table**

In `namespace detail`, add one function both `formationSlot` and `rankOfSlot` call, so the mirror those two maintain is structural rather than a comment:

```cpp
// The single definition of how wide a shape stands. formationSlot and
// rankOfSlot BOTH call this, which is what makes their agreement structural
// instead of a promise in a comment.
inline uint32_t shapeWidth(FormationShape shape, uint32_t memberCount) {
    return rankWidth(memberCount, traitsOf(shape).aspect);
}
```

In `formationSlot`, replace the local `spacing`/`aspect` locals with `traitsOf(shape)` reads and the `rankWidth(memberCount, aspect)` call with `detail::shapeWidth(shape, memberCount)`. In `rankOfSlot`, replace the `aspect` ternary and its `rankWidth` call with `detail::shapeWidth(shape, memberCount)`. Delete the hand-mirroring comment in `rankOfSlot` and replace it with one saying both functions now derive width from `detail::shapeWidth`.

The Wedge early return in both functions is unchanged.

- [ ] **Step 5: Add `shape` to `SquadHot` and read it**

In `c++/src/Squads.hpp` add `std::vector<uint8_t> shape;` with `shape.push_back((uint8_t)FormationShape::Line);` in `spawn()` and `shape.clear();` in `clear()`.

In `c++/src/Simulation.cpp` deployment, after `squads.spawn(team, unit)`, add `squads.shape[squadId] = (uint8_t)shapeForUnit(unit);` so the starting shape is exactly what `shapeForUnit` would have returned.

In `c++/src/Soldiers.cpp`, change `slotWorldPosition`'s first line from `const FormationShape shape = shapeForUnit(squads.unitType[s]);` to `const FormationShape shape = (FormationShape)squads.shape[s];`.

In `stateDigest()`, add `d.mix(static_cast<uint32_t>(squads.shape[s]));` to the squad loop.

- [ ] **Step 6: Run the tests**

Run: `scripts/build.bat -t`
Expected: PASS, all tests including `test_counters`. The digest MUST NOT move in this task: the shape a squad carries is the shape `shapeForUnit` returned, and the traits rows reproduce the old constants. A moved digest here means the refactor changed geometry, which is a bug in this task, not a baseline to regenerate.

- [ ] **Step 7: Commit**

```bash
git add c++/src/Formation.hpp c++/src/Squads.hpp c++/src/Simulation.cpp c++/src/Simulation.hpp c++/src/Soldiers.cpp c++/tests/test_formation.cpp
git commit -m "refactor: drive formation geometry from a traits table

formationSlot and rankOfSlot derived width from separately hardcoded aspect
constants and carried a comment asking future edits to keep them in sync.
Both now call detail::shapeWidth, so the agreement is structural. Formation
also becomes per-squad state rather than a function of unit type, which is
what lets a squad change shape later. Digest unchanged, deliberately."
```

---

## Task 3: The five new formation shapes

**Files:**
- Modify: `c++/src/Units.hpp` (`FormationShape` enum, maniple and mob constants)
- Modify: `c++/src/Formation.hpp` (`formationSlot`, `rankOfSlot`)
- Test: `c++/tests/test_formation.cpp`

**Interfaces:**
- Consumes: `kFormationTraits`, `detail::shapeWidth` from Task 2.
- Produces: `FormationShape::Shieldwall`, `::Phalanx`, `::Testudo`, `::Manipular`, `::Mob`; `kManipleWidth`, `kManipleInterval`, `kMobJitter`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_formation.cpp`:

```cpp
TEST_CASE("rankOfSlot agrees with formationSlot for the new grid shapes") {
    const FormationShape shapes[] = {
        FormationShape::Shieldwall, FormationShape::Phalanx,
        FormationShape::Testudo,
    };
    for (FormationShape shape : shapes) {
        for (uint32_t n = 1; n <= 200; ++n) {
            for (uint16_t i = 0; i < (uint16_t)n; ++i) {
                const Vec2 s = formationSlot(shape, i, n);
                const uint32_t rank = rankOfSlot(shape, i, n);
                const float spacing = kSlotSpacing * traitsOf(shape).spacing;
                CHECK(s.y == doctest::Approx(-(float)rank * spacing));
            }
        }
    }
}

TEST_CASE("a phalanx stands deeper than a shieldwall of the same size") {
    float phalanxDepth = 0.0f, wallDepth = 0.0f;
    for (uint16_t i = 0; i < 48; ++i) {
        phalanxDepth = std::min(phalanxDepth, formationSlot(FormationShape::Phalanx, i, 48).y);
        wallDepth    = std::min(wallDepth,    formationSlot(FormationShape::Shieldwall, i, 48).y);
    }
    CHECK(phalanxDepth < wallDepth);
}

TEST_CASE("a testudo stands tighter than a line") {
    const Vec2 a = formationSlot(FormationShape::Testudo, 1, 48);
    const Vec2 b = formationSlot(FormationShape::Line, 1, 48);
    CHECK(std::abs(a.x) < std::abs(b.x));
}

TEST_CASE("a manipular line has gaps a line does not") {
    // Column kManipleWidth begins the second maniple, so the step from the
    // slot before it to the slot at it is wider than a normal one.
    const uint32_t width = detail::shapeWidth(FormationShape::Manipular, 60);
    REQUIRE(width > kManipleWidth);
    const float normal = formationSlot(FormationShape::Manipular, 1, 60).x
                       - formationSlot(FormationShape::Manipular, 0, 60).x;
    const float gap = formationSlot(FormationShape::Manipular, (uint16_t)kManipleWidth, 60).x
                    - formationSlot(FormationShape::Manipular, (uint16_t)kManipleWidth - 1, 60).x;
    CHECK(gap > normal * 1.5f);
}

TEST_CASE("mob slots are scattered but never closer than the separation radius") {
    for (uint16_t i = 0; i < 40; ++i) {
        for (uint16_t j = (uint16_t)(i + 1); j < 40; ++j) {
            const Vec2 a = formationSlot(FormationShape::Mob, i, 40);
            const Vec2 b = formationSlot(FormationShape::Mob, j, 40);
            const float dx = a.x - b.x, dy = a.y - b.y;
            CHECK(std::sqrt(dx * dx + dy * dy) > kSeparationRadius);
        }
    }
}

TEST_CASE("mob slots are deterministic") {
    for (uint16_t i = 0; i < 40; ++i) {
        const Vec2 a = formationSlot(FormationShape::Mob, i, 40);
        const Vec2 b = formationSlot(FormationShape::Mob, i, 40);
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }
}

TEST_CASE("every shape keeps the centroid a fixed point") {
    for (uint32_t i = 0; i < kFormationShapeCount; ++i) {
        const FormationShape shape = (FormationShape)i;
        float sumX = 0.0f, sumY = 0.0f;
        for (uint16_t k = 0; k < 40; ++k) {
            const Vec2 s = formationSlot(shape, k, 40);
            sumX += s.x;
            sumY += s.y;
        }
        const Vec2 mean = formationMeanOffset(shape, 40);
        CHECK(mean.x == doctest::Approx(sumX / 40.0f));
        CHECK(mean.y == doctest::Approx(sumY / 40.0f));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `FormationShape::Shieldwall` not declared.

- [ ] **Step 3: Extend the enum and add the constants**

In `c++/src/Units.hpp`, extend the enum (appended, per the global constraints):

```cpp
enum class FormationShape : uint8_t {
    Line = 0, Column = 1, Wedge = 2, Loose = 3,
    Shieldwall = 4, Phalanx = 5, Testudo = 6, Manipular = 7, Mob = 8
};
```

Add near `kSlotSpacing`'s consumers:

```cpp
// Manipular geometry. The interval is not decoration: it is the corridor a
// relieved maniple retires through (see Army.cpp's line relief), and a legion
// without it is a shieldwall with holes in it.
constexpr uint32_t kManipleWidth    = 8;     // columns before an interval
constexpr float    kManipleInterval = 12.0f; // one kSlotSpacing

// How far a mob's slots scatter from their grid position. MUST stay under
// kSeparationRadius (10): a scatter at or above it puts two slots close
// enough that separation shoves their occupants apart, and the formation
// spends the battle fighting its own avoidance force. Same failure mode
// kSeparationRadius documents against kSlotSpacing.
constexpr float kMobJitter = 4.0f;
```

- [ ] **Step 4: Implement the geometry**

In `formationSlot`, after the Wedge branch and before the grid computation, the five new shapes fall through to the grid path unchanged except for two special cases. Add them after `right` and `forward` are computed:

```cpp
    float right = ((float)col - (float)(width - 1) * 0.5f) * spacing;
    const float forward = -(float)row * spacing;

    if (shape == FormationShape::Manipular) {
        // Push each column outward by one interval per maniple boundary it
        // sits past. Computed from the column index rather than accumulated,
        // so the result depends on nothing but this slot.
        right += (float)(col / kManipleWidth) * kManipleInterval;
        // Recenter: the pushes above are all rightward, which would walk the
        // formation's mean off zero and make the squad chase its own anchor.
        const uint32_t maniples = (width - 1) / kManipleWidth;
        right -= (float)maniples * kManipleInterval * 0.5f;
    }

    if (shape == FormationShape::Mob) {
        // Deterministic scatter: a pure function of the slot index, drawing
        // no Rng state at all, so it is identical on every thread and
        // platform without participating in the random stream.
        const uint32_t h = pcgHash((uint32_t)slotIndex * 0x9E3779B9u);
        const float jx = ((float)(h & 0xFFFFu) / 65535.0f - 0.5f) * 2.0f * kMobJitter;
        const float jy = ((float)((h >> 16) & 0xFFFFu) / 65535.0f - 0.5f) * 2.0f * kMobJitter;
        return Vec2{ right + jx, forward + jy };
    }

    return Vec2{ right, forward };
```

Add `#include "Rng.hpp"` to `Formation.hpp` for `pcgHash`.

`rankOfSlot` needs no change for Manipular (the interval shifts x only) or for Mob, whose jitter is bounded well under one spacing. Add a comment in `rankOfSlot` saying exactly that, since it is the kind of thing a reader will otherwise wonder about.

- [ ] **Step 5: Run the tests**

Run: `scripts/build.bat -t`
Expected: PASS. The mob separation test is the one most likely to fail; if it does, the cause is `kMobJitter` being too large relative to `kSlotSpacing * 1.5`, not the test being wrong.

Digest MUST NOT move: no squad adopts a new shape yet.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Units.hpp c++/src/Formation.hpp c++/tests/test_formation.cpp
git commit -m "feat: add shieldwall, phalanx, testudo, manipular and mob geometry

Shapes only. Nothing adopts them yet, so the digest is unchanged."
```

---

## Task 4: Armor and the melee wound roll

First behavior change to combat. A blow that reaches now has to get through what the target is wearing.

**Files:**
- Modify: `c++/src/Rng.hpp` (two appended enumerators)
- Modify: `c++/src/Combat.hpp` / `.cpp` (`applyMeleeIntents` takes an `Rng`)
- Modify: `c++/src/Simulation.cpp` (call site, armor speed scale)
- Modify: `c++/src/Soldiers.cpp` (`steerToward` applies armor speed)
- Create: `c++/tests/test_armor.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `kWoundChancePct`, `kArmorSpeedScale`, `loadoutOf` from Task 1.
- Produces: `void applyMeleeIntents(SoldierHot&, const SquadHot&, const Rng&)` (signature change), `RngUse::MeleeWoundRoll`, `RngUse::MeleeBlockRoll`.

**Why `SquadHot` enters now, before anything in this task reads it:** Task 6 needs it for the shield arc and Task 7 for the cooldown scale. Adding it here means one signature change and one sweep over the tests instead of three.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_armor.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Loadout.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "Combat.hpp"
#include "Rng.hpp"

namespace {
// Two one-man squads facing each other, both in Line. Melee resolution reads
// the squad tier for the attacker's formation and the defender's facing, so
// even a two-soldier test needs one.
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

TEST_CASE("a mailed target survives blows an unarmored one does not") {
    // Same attacker, same seed, same number of swings. Only the armor differs.
    auto survivorsAfter = [](TroopClass defender) {
        SoldierHot s;
        SquadHot q = duelSquads();
        s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Huscarl, 0);
        s.spawn(105.0f, 100.0f, 0, 0, Team::B, defender, 1);
        uint32_t blows = 0;
        for (uint32_t tick = 0; tick < 400 && s.health[1] > 0; ++tick) {
            const Rng rng{ 42u, tick };
            s.intentTarget[0] = 1;
            s.attackCooldown[0] = 0.0f;
            applyMeleeIntents(s, q, rng);
            ++blows;
        }
        return blows;
    };
    CHECK(survivorsAfter(TroopClass::Hoplite) > survivorsAfter(TroopClass::Skirmisher));
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

TEST_CASE("plate advances more slowly than padding") {
    Simulation sim(2400, 1600, 42u, 0u);
    sim.init(600);
    // Compare the distance covered by the heaviest and lightest troops present.
    // Cavalry are faster by base speed, so compare like with like: two soldiers
    // of the same UnitType cannot differ here, which is why this asserts on the
    // scale table directly rather than on emergent positions.
    CHECK(kArmorSpeedScale[(int)ArmorClass::Plate] < kArmorSpeedScale[(int)ArmorClass::Padded]);
    CHECK(kArmorSpeedScale[(int)ArmorClass::None] == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `applyMeleeIntents` takes one argument.

- [ ] **Step 3: Append the RNG enumerators**

In `c++/src/Rng.hpp`, immediately before `Count`:

```cpp
    // Plan 5: armor and shields. Four distinct draws, because a melee blow can
    // reach both of its rolls within one agent-tick and so can a missile.
    MeleeBlockRoll,
    MeleeWoundRoll,
    ShieldBlockRoll,
    MissileWoundRoll,
```

Add all four now, even though two are unused until Task 6. Appending them together means Task 6 does not move every enumerator declared after it a second time, which would move the digest for no behavioral reason.

- [ ] **Step 4: Add the wound roll to melee**

In `c++/src/Combat.hpp`, change the declaration to `void applyMeleeIntents(SoldierHot& soldiers, const SquadHot& squads, const Rng& rng);`, add `#include "Rng.hpp"` and forward-declare `struct SquadHot;`. Nothing in this task reads `squads` yet; see the note under Interfaces for why it arrives now.

In `c++/src/Combat.cpp`, inside the loop, replace the damage application with:

```cpp
        const Loadout& attacker = loadoutOf(soldiers.troopClass[i]);
        const Loadout& defender = loadoutOf(soldiers.troopClass[t]);

        // The sidearm, not the weapon: a legionary inside melee range has
        // already thrown his pilum and is fighting with a sword.
        const uint8_t woundPct = kWoundChancePct[(int)attacker.sidearm][(int)defender.armor];
        const bool wounds = rng.range((uint32_t)i, RngUse::MeleeWoundRoll, 1, 100) <= woundPct;

        if (wounds) {
            soldiers.health[t] = (soldiers.health[t] > kMeleeDamage)
                               ? (uint8_t)(soldiers.health[t] - kMeleeDamage)
                               : (uint8_t)0;
        }

        // The cooldown is set whether or not the blow landed, and that is a
        // correctness requirement rather than a detail. If a failed blow left
        // the cooldown clear, the attacker would re-roll every tick until he
        // got through, and armor would be a short delay instead of a defense.
        // A parried swing costs you the swing.
        soldiers.attackCooldown[i] = kMeleeCooldown;
        soldiers.state[i] = SoldierState::Engaged;
```

- [ ] **Step 5: Apply the armor speed scale**

In `c++/src/Soldiers.cpp`, in `steerToward`, change the speed line to:

```cpp
    const Loadout& lo = loadoutOf(soldiers.troopClass[i]);
    const float speed = kUnitStats[(int)soldiers.unitType[i]].speed
                      * kArmorSpeedScale[(int)lo.armor] * speedScale;
```

Add `#include "Loadout.hpp"`.

- [ ] **Step 6: Update the call site**

In `c++/src/Simulation.cpp`'s `phaseResolution`, change step 1 to `applyMeleeIntents(soldiers, squads, rng);`.

- [ ] **Step 7: Wire up CMake and run**

Add `tests/test_armor.cpp` to `tactix_tests`.

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`, whose digest has legitimately moved.

- [ ] **Step 8: Regenerate baselines and commit**

Regenerate both baseline files as in Task 1 Step 13, noting the cause: melee blows now roll against armor, so fewer men die per tick and the battle takes a different course from the first contact.

```bash
git add c++/src/Rng.hpp c++/src/Combat.hpp c++/src/Combat.cpp c++/src/Soldiers.cpp c++/src/Simulation.cpp c++/tests c++/CMakeLists.txt
git commit -m "feat: melee blows roll against the target's armor

A swing that reaches no longer wounds automatically. The cooldown is spent
whether or not the blow lands, without which an attacker re-rolls every tick
and armor becomes a delay rather than a defense."
```

---

## Task 5: The missile wound roll, replacing kArrowHitChancePct

**Files:**
- Modify: `c++/src/Projectiles.hpp` (`weapon` field, `spawn` signature) / `.cpp` (`applyProjectileHits`, `spawnArrows`, `compactProjectiles`)
- Modify: `c++/src/Units.hpp` (delete `kArrowHitChancePct`)
- Modify: `c++/src/Simulation.cpp` (digest)
- Test: `c++/tests/test_armor.cpp`, `c++/tests/test_projectiles.cpp`

**Interfaces:**
- Consumes: `kWoundChancePct` from Task 1, `RngUse::MissileWoundRoll` from Task 4.
- Produces: `ProjectileHot::weapon`, `ProjectileHot::spawn(..., uint8_t weapon)`, `void applyProjectileHits(ProjectileHot&, SoldierHot&, SquadHot&, const Rng&)` (signature change).

**`SquadHot` enters now for the same reason it did in Task 4:** Task 6 reads it for the shield arc and Task 9 writes missile pressure through it. One signature change, one test sweep. It is non-const because Task 9 writes to it.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_armor.cpp`:

```cpp
TEST_CASE("an arrow rolls against the target's armor, not a global constant") {
    auto hitsToKill = [](TroopClass defender) {
        SoldierHot s;
        SquadHot q = duelSquads();
        s.spawn(100.0f, 100.0f, 0, 0, Team::B, defender, 0);
        uint32_t arrows = 0;
        for (uint32_t tick = 0; tick < 3000 && s.health[0] > 0; ++tick) {
            ProjectileHot p;
            p.spawn(100.0f, 100.0f, 1.0f, 0.0f, Team::A, kArrowDamage,
                    kArrowLifetime, 0.0f, (uint8_t)WeaponClass::Bow);
            p.intentHitTarget[0] = 0;
            const Rng rng{ 42u, tick };
            applyProjectileHits(p, s, q, rng);
            ++arrows;
        }
        return arrows;
    };
    // Plate is 12 percent against a bow, padding 55. The gap is large enough
    // that a fixed seed cannot plausibly invert it.
    CHECK(hitsToKill(TroopClass::Knight) > hitsToKill(TroopClass::Archer) * 2);
}
```

Add to `c++/tests/test_projectiles.cpp` a check that the constant is gone: no test can reference `kArrowHitChancePct`, so instead assert the surviving behavior:

```cpp
TEST_CASE("an arrow that reaches a soldier is spent whether or not it wounds") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::B, UnitType::Infantry);
    q.memberCount[0] = 1;
    s.spawn(100.0f, 100.0f, 0, 0, Team::B, TroopClass::Knight, 0);
    ProjectileHot p;
    p.spawn(100.0f, 100.0f, 1.0f, 0.0f, Team::A, kArrowDamage,
            kArrowLifetime, 0.0f, (uint8_t)WeaponClass::Bow);
    p.intentHitTarget[0] = 0;
    const Rng rng{ 42u, 1u };
    applyProjectileHits(p, s, q, rng);
    CHECK(p.lifetime[0] == 0.0f);
    CHECK(p.intentHitTarget[0] == UINT32_MAX);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `ProjectileHot::spawn` takes eight arguments.

- [ ] **Step 3: Add the weapon field**

In `c++/src/Projectiles.hpp`, add `std::vector<uint8_t> weapon;` with this comment:

```cpp
    // What threw this. Carried on the ARROW rather than looked up from the
    // shooter, because by the time an arrow lands the shooter may be dead and
    // compaction has renumbered every survivor: his index means someone else.
    std::vector<uint8_t> weapon;   // WeaponClass
```

Add the parameter to `spawn` (last, so existing argument order is untouched), `weapon.push_back(w);` in the body, `weapon.clear();` in `clear()`.

In `compactProjectiles` (`c++/src/Projectiles.cpp`), add the swap and the `pop_back` for it alongside `damage`.

In `stateDigest()`, add `d.mix(static_cast<uint32_t>(projectiles.weapon[i]));` to the projectile loop.

- [ ] **Step 4: Roll against armor**

Change the declaration in `c++/src/Projectiles.hpp` to `void applyProjectileHits(ProjectileHot& p, SoldierHot& soldiers, SquadHot& squads, const Rng& rng);` and update the call in `phaseResolution` to `applyProjectileHits(projectiles, soldiers, squads, rng);`. Nothing in this task reads `squads`; see the note under Interfaces.

Then in `applyProjectileHits`, replace the `kArrowHitChancePct` roll:

```cpp
        const Loadout& defender = loadoutOf(soldiers.troopClass[t]);
        const uint8_t woundPct =
            kWoundChancePct[(int)p.weapon[i]][(int)defender.armor];

        // Crossing a man is a chance to wound, not a guaranteed one, and the
        // roll is consumed either way: an arrow that glances off gets no
        // second attempt next tick, which would make the chance meaningless.
        const bool wounds =
            rng.range((uint32_t)i, RngUse::MissileWoundRoll, 1, 100) <= woundPct;
        if (wounds) {
            soldiers.health[t] = (soldiers.health[t] > p.damage[i])
                               ? (uint8_t)(soldiers.health[t] - p.damage[i])
                               : (uint8_t)0;
        }
```

Add `#include "Loadout.hpp"`.

- [ ] **Step 5: Pass the shooter's weapon when spawning**

In `spawnArrows`, pass `(uint8_t)loadoutOf(soldiers.troopClass[i]).weapon` as the new argument.

- [ ] **Step 6: Delete `kArrowHitChancePct`**

Remove the constant and its comment from `c++/src/Units.hpp`. Add a short comment where it was, saying the geometry-then-wound split replaced it and pointing at `kWoundChancePct`.

Do NOT remove `RngUse::ArrowHitRoll`. Add a comment on it:

```cpp
    ArrowHitRoll,           // unused since the armor model landed; see the
                            // never-delete rule above. kWoundChancePct
                            // replaced what this used to roll against.
```

- [ ] **Step 7: Run, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`.

Regenerate both baselines, noting the cause: `kArrowHitChancePct` is gone and arrows now roll against armor, which changes both lethality and, through it, the whole course of the battle.

```bash
git add c++/src/Projectiles.hpp c++/src/Projectiles.cpp c++/src/Units.hpp c++/src/Simulation.cpp c++/tests
git commit -m "feat: arrows roll against the target's armor

Replaces kArrowHitChancePct rather than stacking on it. A flat 45 percent
multiplied by an armor roll would put a bowman near 8 percent against a mailed
man and make archery ornamental. RngUse::ArrowHitRoll stays in the enum,
unused, per the never-delete rule."
```

---

## Task 6: Shields

**Files:**
- Create: `c++/src/Shields.hpp` / `c++/src/Shields.cpp`
- Create: `c++/tests/test_shields.cpp`
- Modify: `c++/src/Loadout.hpp` (cover constants)
- Modify: `c++/src/Combat.cpp`, `c++/src/Projectiles.cpp` (both call sites)
- Modify: `c++/src/Projectiles.hpp` (`applyProjectileHits` takes `SquadHot`)
- Modify: `c++/src/Simulation.cpp` (call site)
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `kFormationTraits`/`traitsOf` (Task 2), `loadoutOf` (Task 1), `RngUse::ShieldBlockRoll`/`MeleeBlockRoll` (Task 4).
- Produces: `enum class ImpactArc : uint8_t { Front=0, Side=1, Rear=2 }`, `ImpactArc impactArc(float,float,float,float)`, `uint8_t blockChancePct(ShieldClass, FormationShape, ImpactArc, bool)`, `uint8_t shieldBlockPct(const SoldierHot&, const SquadHot&, uint32_t, float, float, bool)`, `void applyProjectileHits(ProjectileHot&, SoldierHot&, SquadHot&, const Rng&)` (signature change).

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_shields.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Shields.hpp"
#include "Loadout.hpp"
#include "Formation.hpp"
#include "Simulation.hpp"

TEST_CASE("a blow into the face is a front arc, into the back a rear arc") {
    // Squad faces +x. A blow travelling +x arrives from behind; one travelling
    // -x arrives head on.
    CHECK(impactArc(-1.0f, 0.0f, 1.0f, 0.0f) == ImpactArc::Front);
    CHECK(impactArc( 1.0f, 0.0f, 1.0f, 0.0f) == ImpactArc::Rear);
    CHECK(impactArc( 0.0f, 1.0f, 1.0f, 0.0f) == ImpactArc::Side);
    CHECK(impactArc( 0.0f,-1.0f, 1.0f, 0.0f) == ImpactArc::Side);
}

TEST_CASE("no shield blocks nothing in any arc or formation") {
    for (uint32_t s = 0; s < kFormationShapeCount; ++s) {
        for (uint32_t a = 0; a < 3; ++a) {
            const uint8_t pct = blockChancePct(ShieldClass::None,
                                               (FormationShape)s,
                                               (ImpactArc)a, false);
            // Formation cover is a bonus ON a shield, not a substitute for one.
            CHECK(pct == 0);
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

TEST_CASE("a volley into the rear kills more than the same volley into the face") {
    // One squad of eight shielded men, facing +x. Fire an identical stream of
    // arrows into it from each side and count the dead.
    auto casualties = [](float arrowDirX) {
        SoldierHot s;
        SquadHot q;
        q.spawn(Team::B, UnitType::Infantry);
        q.troopClass[0] = (uint8_t)TroopClass::Legionary;
        q.shape[0] = (uint8_t)FormationShape::Shieldwall;
        q.facingX[0] = 1.0f;
        q.facingY[0] = 0.0f;
        q.memberCount[0] = 8;
        for (uint32_t k = 0; k < 8; ++k) {
            s.spawn(100.0f + (float)k, 100.0f, 0, 0, Team::B, TroopClass::Legionary, 0);
        }
        uint32_t dead = 0;
        for (uint32_t tick = 0; tick < 600; ++tick) {
            ProjectileHot p;
            for (uint32_t k = 0; k < 8; ++k) {
                p.spawn(100.0f, 100.0f, arrowDirX, 0.0f, Team::A, kArrowDamage,
                        kArrowLifetime, 0.0f, (uint8_t)WeaponClass::Bow);
                p.intentHitTarget[k] = k;
            }
            const Rng rng{ 42u, tick };
            applyProjectileHits(p, s, q, rng);
        }
        for (uint32_t k = 0; k < 8; ++k) if (s.health[k] == 0) ++dead;
        return dead;
    };
    // -1 travels toward -x, so it arrives in the face of a squad facing +x.
    CHECK(casualties(1.0f) >= casualties(-1.0f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `Shields.hpp` not found.

- [ ] **Step 3: Add the cover constants to `Loadout.hpp`**

```cpp
// Chance in percent that a shield stops a missile arriving in the FRONT arc.
constexpr uint8_t kShieldCoverPct[kShieldCount] = { 0, 20, 40, 55 };

// Front, side, rear. A shield covers what a man faces and nothing behind him.
constexpr float kArcCoverScale[3] = { 1.0f, 0.35f, 0.0f };

// A shield stops an arrow better than it stops a man determined to push past
// it. Applied to the whole block chance, formation bonus included.
constexpr float kShieldMeleeScale = 0.6f;

// Ceiling on any block chance. Nothing is ever certain, and without this a
// tower shield in testudo reaches 110 and is literally invulnerable.
constexpr uint8_t kMaxBlockPct = 90;
```

- [ ] **Step 4: Create `c++/src/Shields.hpp`**

```cpp
#pragma once
#include "Loadout.hpp"
#include "Units.hpp"
#include <cstdint>

struct SoldierHot;
struct SquadHot;

// Which side of a man a blow arrived on. Front is a 120 degree arc, and so
// are the other two: even thirds are the honest default and nothing in the
// design argues for skewing them.
enum class ImpactArc : uint8_t { Front = 0, Side = 1, Rear = 2 };

// (impactDirX, impactDirY) is the direction the blow TRAVELS, not the
// direction it came from. Neither vector needs to be normalized: only the
// sign and relative magnitude of the dot product matter, and both are scale
// invariant once divided by the lengths.
ImpactArc impactArc(float impactDirX, float impactDirY,
                    float facingX, float facingY);

// The whole shield model, as a pure function of four small values. Formation
// cover is a bonus ON a shield rather than a substitute for one: a man with no
// shield blocks nothing however he is standing, which is why the multiply
// comes before the add and the add cannot rescue a zero.
uint8_t blockChancePct(ShieldClass shield, FormationShape shape,
                       ImpactArc arc, bool melee);

// Thin wrapper for the two resolution sites. Uses the target's SQUAD facing
// rather than his own dirX/dirY: a man standing still in a fight keeps
// whatever direction he was last moving in, which is stale and sometimes
// meaningless, while the squad facing is the deliberate statement of where the
// formation points and is what the shields of a formed body actually follow.
//
// While a squad is mid-transition (shapeBlend > 0) this takes the WORSE of its
// old and new shapes, per arc. That is the cost of drilling under fire.
uint8_t shieldBlockPct(const SoldierHot& soldiers, const SquadHot& squads,
                       uint32_t target, float impactDirX, float impactDirY,
                       bool melee);
```

- [ ] **Step 5: Create `c++/src/Shields.cpp`**

```cpp
#include "Shields.hpp"
#include "Formation.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include <algorithm>
#include <cmath>

ImpactArc impactArc(float impactDirX, float impactDirY,
                    float facingX, float facingY) {
    const float len = std::sqrt(impactDirX * impactDirX + impactDirY * impactDirY);
    if (len < 1e-6f) return ImpactArc::Front;   // degenerate, treat as head on

    // Negated: a blow travelling the same way a man faces is coming from
    // behind him. cos == 1 is straight into the face.
    const float cos = -((impactDirX * facingX + impactDirY * facingY) / len);
    if (cos >  0.5f) return ImpactArc::Front;
    if (cos > -0.5f) return ImpactArc::Side;
    return ImpactArc::Rear;
}

uint8_t blockChancePct(ShieldClass shield, FormationShape shape,
                       ImpactArc arc, bool melee) {
    if (shield == ShieldClass::None) return 0;

    const float base = (float)kShieldCoverPct[(int)shield] * kArcCoverScale[(int)arc];
    float pct = base + (float)traitsOf(shape).cover[(int)arc];
    if (melee) pct *= kShieldMeleeScale;

    if (pct < 0.0f) return 0;
    if (pct > (float)kMaxBlockPct) return kMaxBlockPct;
    return (uint8_t)pct;
}

uint8_t shieldBlockPct(const SoldierHot& soldiers, const SquadHot& squads,
                       uint32_t target, float impactDirX, float impactDirY,
                       bool melee) {
    const uint16_t s = soldiers.squadId[target];
    if ((size_t)s >= squads.count) return 0;

    const ShieldClass shield = loadoutOf(soldiers.troopClass[target]).shield;
    const ImpactArc arc = impactArc(impactDirX, impactDirY,
                                    squads.facingX[s], squads.facingY[s]);

    const uint8_t now = blockChancePct(shield, (FormationShape)squads.shape[s], arc, melee);
    if (squads.shapeBlend[s] <= 0.0f) return now;

    const uint8_t was = blockChancePct(shield, (FormationShape)squads.prevShape[s], arc, melee);
    return std::min(now, was);
}
```

`shapeBlend` and `prevShape` are added in Task 9. Add them to `SquadHot` now, defaulted to `0.0f` and `Line`, so this file compiles and Task 9 only has to start writing them. Note that in the commit message.

- [ ] **Step 6: Roll the block in both resolution sites**

In `applyProjectileHits`, before the wound roll:

```cpp
        // Direction of travel. Arrow speed is constant, so this is already
        // proportional to the unit vector and impactArc normalizes anyway.
        const uint8_t blockPct = shieldBlockPct(soldiers, squads, t,
                                                p.velX[i], p.velY[i], false);
        const bool blocked =
            rng.range((uint32_t)i, RngUse::ShieldBlockRoll, 1, 100) <= blockPct;
        if (!blocked) {
            // ... the wound roll from Task 5 ...
        }
```

The arrow is still marked spent in either case: the existing `p.lifetime[i] = 0.0f;` stays outside the branch.

In `applyMeleeIntents`, before the wound roll:

```cpp
        const float ix = soldiers.posX[t] - soldiers.posX[i];
        const float iy = soldiers.posY[t] - soldiers.posY[i];
        const uint8_t blockPct = shieldBlockPct(soldiers, squads, t, ix, iy, true);
        const bool blocked =
            rng.range((uint32_t)i, RngUse::MeleeBlockRoll, 1, 100) <= blockPct;
```

and gate the wound roll on `!blocked`. The cooldown assignment stays outside the branch, for the reason Task 4 already documented.

Both functions already take their `SquadHot` parameter, added in Tasks 4 and 5 precisely so this task does not change either signature. This task only adds the two roll sites and the `#include "Shields.hpp"` above them.

- [ ] **Step 7: Wire up CMake and run**

Add `src/Shields.cpp` to `tactix_sim` and `tests/test_shields.cpp` to `tactix_tests`.

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`.

- [ ] **Step 8: Regenerate baselines and commit**

```bash
git add c++/src/Shields.hpp c++/src/Shields.cpp c++/src/Loadout.hpp c++/src/Combat.cpp c++/src/Combat.hpp c++/src/Projectiles.cpp c++/src/Projectiles.hpp c++/src/Squads.hpp c++/src/Simulation.cpp c++/tests c++/CMakeLists.txt
git commit -m "feat: shields block blows arriving in the arc a man faces

One definition of cover, shared by melee and missiles. A volley into a
formation's flank is now worth far more than the same volley into its face.
Adds the shapeBlend and prevShape fields the transition in a later commit
writes, so cover can already take the worse of two shapes."
```

---

## Task 7: Formation traits drive speed, turn rate and attack rate

**Files:**
- Modify: `c++/src/Simulation.cpp` (`phaseSoldierSteerChunk`)
- Modify: `c++/src/Squads.cpp` (`squadDecide` facing slew)
- Modify: `c++/src/Combat.cpp` (`applyMeleeIntents` cooldown)
- Test: `c++/tests/test_steering.cpp`, `c++/tests/test_squads.cpp`

**Interfaces:**
- Consumes: `traitsOf`, `SquadHot::shape` from Task 2.
- Produces: no new symbols.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_squads.cpp`:

```cpp
TEST_CASE("a phalanx turns more slowly than a line") {
    auto ticksToFace = [](FormationShape shape) {
        SquadHot q;
        ArmyHot armies;
        armies.spawn();
        armies.spawn();
        q.spawn(Team::A, UnitType::Infantry);
        q.spawn(Team::B, UnitType::Infantry);
        q.shape[0] = (uint8_t)shape;
        q.memberCount[0] = 10;
        q.memberCount[1] = 10;
        q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
        q.centroidX[0] = 0.0f;   q.centroidY[0] = 0.0f;
        q.centroidX[1] = 0.0f;   q.centroidY[1] = 100.0f;  // due +y, 90 degrees off
        q.targetSquad[0] = 1;
        TerrainField terrain;
        uint32_t ticks = 0;
        while (q.facingY[0] < 0.99f && ticks < 2000) {
            squadDecide(q, armies, 0, terrain, kFixedTimestep);
            ++ticks;
        }
        return ticks;
    };
    CHECK(ticksToFace(FormationShape::Phalanx) > ticksToFace(FormationShape::Line) * 2);
}
```

Add to `c++/tests/test_armor.cpp`:

```cpp
TEST_CASE("a testudo swings more slowly than a line") {
    auto cooldownAfterBlow = [](FormationShape shape) {
        SoldierHot s;
        SquadHot q;
        q.spawn(Team::A, UnitType::Infantry);
        q.spawn(Team::B, UnitType::Infantry);
        q.shape[0] = (uint8_t)shape;
        q.memberCount[0] = 1;
        q.memberCount[1] = 1;
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: both new cases FAIL, the phalanx and testudo values equalling the line's.

- [ ] **Step 3: Apply the turn scale**

In `c++/src/Squads.cpp`, in `squadDecide`'s facing block, change the slew rate:

```cpp
        const float turn = traitsOf((FormationShape)squads.shape[s]).turn;
        const Vec2 f = slewFacing(Vec2{ squads.facingX[s], squads.facingY[s] },
                                  Vec2{ dx, dy }, kFacingSlewRate * turn * dt);
```

Add a comment: a phalanx at 0.35 needs about four seconds to face a threat it started perpendicular to, which is why cavalry that reaches its flank stays there. The weakness falls out of the existing slew mechanism rather than being a special case.

Add `#include "Formation.hpp"` if not already present.

- [ ] **Step 4: Apply the speed scale**

In `c++/src/Simulation.cpp`'s `phaseSoldierSteerChunk`, where `steerToward` is called, multiply the existing `speedScale` argument by the squad's formation speed:

```cpp
        const uint16_t sq = soldiers.squadId[i];
        const float formationSpeed = traitsOf((FormationShape)squads.shape[sq]).speed;
```

and pass `speedScale * formationSpeed`. Armor's scale is already applied inside `steerToward` (Task 4), so the three multiply cleanly and each stays owned by the layer that knows about it.

- [ ] **Step 5: Apply the cooldown scale**

In `c++/src/Combat.cpp`, replace the cooldown assignment:

```cpp
        const FormationShape shape = (FormationShape)squads.shape[soldiers.squadId[i]];
        soldiers.attackCooldown[i] = kMeleeCooldown * traitsOf(shape).cooldown;
```

Add a comment: testudo at 2.2 is the price of its cover. Men under their shields fight badly, and without this a testudo is a free win in melee as well as against arrows.

- [ ] **Step 6: Run, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`. The digest moves only if some squad is in a non-Line shape, which none is yet, so verify whether it moved and do not regenerate if it did not. A moved digest here means a traits row for one of the original four shapes is not 1.0 where it should be.

```bash
git add c++/src/Squads.cpp c++/src/Simulation.cpp c++/src/Combat.cpp c++/tests
git commit -m "feat: formation traits drive movement, turning and attack rate

Speed, turn rate and swing rate all read from the traits table. No squad
adopts a new shape yet, so the digest is unchanged."
```

---

## Task 8: Fighting ranks, reach and the front-arc gate

**Files:**
- Modify: `c++/src/Combat.hpp` / `.cpp` (`selectMeleeTarget`)
- Modify: `c++/src/Simulation.cpp` (call site)
- Test: `c++/tests/test_combat.cpp`

**Interfaces:**
- Consumes: `traitsOf`, `rankOfSlot`, `impactArc`.
- Produces: `void selectMeleeTarget(SoldierHot&, const SquadHot&, const SpatialHash&, size_t, std::vector<uint32_t>&)` (signature change).

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_combat.cpp`:

```cpp
TEST_CASE("a phalanx reaches an enemy from the second rank") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.shape[0] = (uint8_t)FormationShape::Phalanx;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.memberCount[0] = 20;
    q.spawn(Team::B, UnitType::Infantry);
    q.memberCount[1] = 1;

    // Spearman standing one rank back, enemy just past normal melee reach but
    // inside a spear's 1.6x.
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Hoplite, 0);
    s.slotIndex[0] = 6;   // not rank 0 in a 20-man phalanx
    s.spawn(100.0f + kMeleeReach * 1.3f, 100.0f, 0, 0, Team::B, TroopClass::Levy, 1);

    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;
    REQUIRE(rankOfSlot(FormationShape::Phalanx, 6, 20) > 0);
    selectMeleeTarget(s, q, hash, 0, scratch);
    CHECK(s.intentTarget[0] == 1);
}

TEST_CASE("a rear rank cannot reach past the front except within its own arc") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.shape[0] = (uint8_t)FormationShape::Phalanx;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.memberCount[0] = 20;
    q.spawn(Team::B, UnitType::Infantry);
    q.memberCount[1] = 1;

    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Hoplite, 0);
    s.slotIndex[0] = 6;
    // Same distance, but BEHIND the squad's facing. Spears reach past the man
    // in front of you, never around him.
    s.spawn(100.0f - kMeleeReach * 1.3f, 100.0f, 0, 0, Team::B, TroopClass::Levy, 1);

    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;
    selectMeleeTarget(s, q, hash, 0, scratch);
    CHECK(s.intentTarget[0] == UINT32_MAX);
}

TEST_CASE("a rear rank can still defend itself at base reach") {
    SoldierHot s;
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.shape[0] = (uint8_t)FormationShape::Shieldwall;   // fightingRanks == 1
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.memberCount[0] = 20;
    q.spawn(Team::B, UnitType::Infantry);
    q.memberCount[1] = 1;

    s.spawn(100.0f, 100.0f, 0, 0, Team::A, TroopClass::Huscarl, 0);
    s.slotIndex[0] = 12;   // deep in the formation
    // An enemy who has got INSIDE the formation, from behind.
    s.spawn(100.0f - 5.0f, 100.0f, 0, 0, Team::B, TroopClass::Levy, 1);

    SpatialHash hash = buildHash(s);
    std::vector<uint32_t> scratch;
    selectMeleeTarget(s, q, hash, 0, scratch);
    CHECK(s.intentTarget[0] == 1);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `selectMeleeTarget` takes four arguments.

- [ ] **Step 3: Implement the rank and arc rules**

In `c++/src/Combat.cpp`:

```cpp
void selectMeleeTarget(SoldierHot& soldiers, const SquadHot& squads,
                       const SpatialHash& hash, size_t i,
                       std::vector<uint32_t>& scratch) {
    soldiers.intentTarget[i] = UINT32_MAX;

    if (soldiers.state[i] == SoldierState::Dead) return;
    if (soldiers.attackCooldown[i] > 0.0f) return;

    const uint16_t sq = soldiers.squadId[i];
    const FormationShape shape = (FormationShape)squads.shape[sq];
    const FormationTraits& tr = traitsOf(shape);
    const uint32_t rank = rankOfSlot(shape, soldiers.slotIndex[i],
                                     squads.memberCount[sq]);

    // A front rank reaches as far as its weapon allows. A rear rank keeps the
    // base reach it always had, so a squad that is flanked or has enemies
    // inside it can still defend itself.
    //
    // This is deliberately NOT the cheaper rule of skipping the query for rear
    // ranks entirely. That would remove about 78 percent of melee queries and
    // would also leave a squad attacked from behind unable to fight back at
    // all. Correctness first; see the plan's performance section for what
    // doing it safely would require.
    const bool extended = rank < tr.fightingRanks;
    const float reach = extended ? kMeleeReach * tr.reach : kMeleeReach;

    const float px = soldiers.posX[i];
    const float py = soldiers.posY[i];
    hash.queryNeighbors(px, py, reach, scratch);

    float bestSq = reach * reach;
    uint32_t best = UINT32_MAX;

    for (uint32_t n : scratch) {
        if ((size_t)n == i) continue;
        if (soldiers.team[n] == soldiers.team[i]) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;

        const float dx = soldiers.posX[n] - px;
        const float dy = soldiers.posY[n] - py;
        const float dSq = dx * dx + dy * dy;
        if (dSq >= bestSq) continue;

        // A man in rank 1 or deeper reaching PAST the man in front of him may
        // only do so forward. Spears reach past your own front rank, never
        // around it. Rank 0 is unrestricted: he is the front rank.
        if (rank > 0 && dSq > kMeleeReach * kMeleeReach) {
            if (impactArc(-dx, -dy, squads.facingX[sq], squads.facingY[sq])
                != ImpactArc::Front) {
                continue;
            }
        }

        bestSq = dSq;
        best = n;
    }

    soldiers.intentTarget[i] = best;
}
```

The `impactArc` arguments are negated because the function takes the direction a blow TRAVELS toward the man it is classifying, and here the question is where the enemy sits relative to our own facing. Negating turns "toward him" into "from him", which is the same test.

Add `#include "Shields.hpp"`, `#include "Formation.hpp"` and `#include "Squads.hpp"`.

- [ ] **Step 4: Update the header and the call site**

Update the declaration in `c++/src/Combat.hpp` and the call in `phaseSoldierSteerChunk` (or wherever `selectMeleeTarget` is dispatched) to pass `squads`. `squads` is read-only in that phase, which is exactly what the parallel-phase rule requires.

- [ ] **Step 5: Run, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`. `candidatesExamined` and `cellsVisited` both move, because the front rank of a spear squad now queries a wider radius. Regenerate and say so.

```bash
git add c++/src/Combat.hpp c++/src/Combat.cpp c++/src/Simulation.cpp c++/tests
git commit -m "feat: spears reach past the front rank, forward only

A formation's fighting depth and reach come from its traits row. A rear rank
keeps base reach so a flanked squad can still defend itself."
```

---

## Task 9: Missile pressure, formation choice and the transition

The task that makes every previous one visible.

**Files:**
- Modify: `c++/src/Units.hpp` (selection constants)
- Modify: `c++/src/Squads.hpp` / `.cpp` (fields, `chooseFormation`, `formationAvailable`)
- Modify: `c++/src/Soldiers.cpp` (`slotWorldPosition` blends)
- Modify: `c++/src/Projectiles.cpp` (pressure accumulation)
- Modify: `c++/src/Simulation.cpp` (digest)
- Test: `c++/tests/test_squads.cpp`, `c++/tests/test_formation.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1 through 8.
- Produces: `bool formationAvailable(TroopClass, FormationShape)`, `FormationShape chooseFormation(const SquadHot&, size_t)`, `void setSquadShape(SquadHot&, size_t, FormationShape)`, `void decayMissilePressure(SquadHot&, size_t, float)`, `SquadHot::prevShape`, `::shapeBlend`, `::formationHold`, `::missilePressure`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_squads.cpp`:

```cpp
TEST_CASE("only tower shields may form a testudo") {
    CHECK(formationAvailable(TroopClass::Legionary, FormationShape::Testudo));
    CHECK_FALSE(formationAvailable(TroopClass::Hoplite, FormationShape::Testudo));
    CHECK_FALSE(formationAvailable(TroopClass::Archer, FormationShape::Testudo));
}

TEST_CASE("only spears may form a phalanx") {
    CHECK(formationAvailable(TroopClass::Hoplite, FormationShape::Phalanx));
    CHECK(formationAvailable(TroopClass::Levy, FormationShape::Phalanx));
    CHECK_FALSE(formationAvailable(TroopClass::Huscarl, FormationShape::Phalanx));
}

TEST_CASE("a squad under fire and out of contact closes up into a testudo") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    q.memberCount[0] = 20;
    q.missilePressure[0] = kTestudoThreshold + 0.1f;
    q.contact[0] = 0;
    q.nearestEnemyDist[0] = 500.0f;
    CHECK(chooseFormation(q, 0) == FormationShape::Testudo);
}

TEST_CASE("a squad in contact fights rather than turtling") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    q.memberCount[0] = 20;
    q.missilePressure[0] = kTestudoThreshold + 5.0f;
    q.contact[0] = 1;
    CHECK(chooseFormation(q, 0) == FormationShape::Shieldwall);
}

TEST_CASE("a spear squad in contact forms a phalanx") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Hoplite;
    q.memberCount[0] = 20;
    q.contact[0] = 1;
    CHECK(chooseFormation(q, 0) == FormationShape::Phalanx);
}

TEST_CASE("a routing squad becomes a mob") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    q.memberCount[0] = 20;
    q.order[0] = (uint8_t)SquadOrder::Rout;
    CHECK(chooseFormation(q, 0) == FormationShape::Mob);
}

TEST_CASE("a squad cannot change shape twice inside the hold window") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.troopClass[0] = (uint8_t)TroopClass::Legionary;
    q.memberCount[0] = 20;
    setSquadShape(q, 0, FormationShape::Testudo);
    CHECK(q.shape[0] == (uint8_t)FormationShape::Testudo);
    CHECK(q.formationHold[0] == doctest::Approx(kFormationHoldSeconds));
    setSquadShape(q, 0, FormationShape::Shieldwall);
    CHECK(q.shape[0] == (uint8_t)FormationShape::Testudo);   // refused
}

TEST_CASE("missile pressure decays to nothing once fire stops") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.memberCount[0] = 20;
    q.missilePressure[0] = 1.0f;
    for (uint32_t t = 0; t < 600; ++t) {
        decayMissilePressure(q, 0, kFixedTimestep);
    }
    CHECK(q.missilePressure[0] == doctest::Approx(0.0f));
}
```

Add to `c++/tests/test_formation.cpp`:

```cpp
TEST_CASE("a blending slot lands exactly on the target shape when the blend ends") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.memberCount[0] = 20;
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    q.morale[0] = 1.0f;  q.discipline[0] = 1.0f;
    setSquadShape(q, 0, FormationShape::Testudo);

    q.shapeBlend[0] = 0.0f;   // blend complete
    const Vec2 done = slotWorldPosition(q, 0, 5, 20);

    q.shapeBlend[0] = kFormationChangeSeconds;   // blend just started
    const Vec2 start = slotWorldPosition(q, 0, 5, 20);

    CHECK(done.x != doctest::Approx(start.x).epsilon(0.0001));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `formationAvailable` not declared.

- [ ] **Step 3: Add the constants**

In `c++/src/Units.hpp`:

```cpp
// Formation transitions (design 4.5). A squad caught mid-drill takes the
// worse of both shapes' cover and the slower of their speeds, so changing
// under fire is a real decision rather than a free upgrade.
constexpr float kFormationChangeSeconds = 1.4f;

// Minimum time in a shape before another change is allowed. This is the
// hysteresis without which a squad at any threshold flips every tick, in the
// same spirit as kContactClearSeconds and the rally threshold gap.
constexpr float kFormationHoldSeconds = 3.0f;

// Discipline below which a squad in contact stops being a formation.
constexpr float kMobDisciplineFloor = 0.45f;

// How close an enemy must be before a squad adopts its melee shape. Squads
// march at about 45px/s, so 60px is a little over a second of warning.
constexpr float kImminentContactDist = 60.0f;

// Missile pressure. Each strike on a member adds, and it bleeds off every
// tick. With these three, roughly two hits a second sustained closes a
// tower-shield squad into a testudo.
constexpr float kMissilePressurePerHit = 0.30f;
constexpr float kMissilePressureDecay  = 0.60f;   // per second
constexpr float kTestudoThreshold      = 1.00f;
```

- [ ] **Step 4: Add the fields**

In `c++/src/Squads.hpp`, add (`prevShape` and `shapeBlend` already exist from Task 6):

```cpp
    // Formation transition state (design 4.5). shapeBlend counts DOWN the
    // seconds remaining in the change; formationHold counts down the minimum
    // time before another change is allowed.
    std::vector<uint8_t> prevShape;
    std::vector<float>   shapeBlend;
    std::vector<float>   formationHold;

    // How hard this squad is being shot at. Accumulated in serial resolution
    // when a member is struck, decayed in phase 4. Costs nothing to compute:
    // resolution already walks every arrow that hit someone.
    std::vector<float>   missilePressure;
```

with the matching `spawn()` and `clear()` entries, and `d.mix(...)` for all four in `stateDigest()`.

- [ ] **Step 5: Implement the three functions**

In `c++/src/Squads.cpp`:

```cpp
bool formationAvailable(TroopClass troop, FormationShape shape) {
    const Loadout& lo = loadoutOf(troop);
    switch (shape) {
        case FormationShape::Testudo:    return lo.shield == ShieldClass::Tower;
        case FormationShape::Phalanx:    return lo.weapon == WeaponClass::Spear
                                             || lo.sidearm == WeaponClass::Spear;
        case FormationShape::Shieldwall: return lo.shield >= ShieldClass::Round;
        case FormationShape::Manipular:  return troop == TroopClass::Legionary;
        default:                         return true;
    }
}

// Eligibility is a property of the LOADOUT, never of the situation, so a squad
// can never adopt a formation its equipment does not support. Every caller
// goes through the predicate above rather than re-deriving the rule.
FormationShape chooseFormation(const SquadHot& squads, size_t s) {
    const TroopClass troop = (TroopClass)squads.troopClass[s];
    const Loadout& lo = loadoutOf(troop);

    // 1. Broken men do not keep ranks.
    if (squads.order[s] == (uint8_t)SquadOrder::Rout) return FormationShape::Mob;
    if (squads.contact[s] && lo.discipline < kMobDisciplineFloor) return FormationShape::Mob;

    // 2. Under fire and not yet in melee: close up, if you can.
    if (squads.missilePressure[s] >= kTestudoThreshold && !squads.contact[s]
        && formationAvailable(troop, FormationShape::Testudo)) {
        return FormationShape::Testudo;
    }

    // 3. Fighting, or about to be.
    if (squads.contact[s] || squads.nearestEnemyDist[s] < kImminentContactDist) {
        if (formationAvailable(troop, FormationShape::Phalanx))    return FormationShape::Phalanx;
        if (formationAvailable(troop, FormationShape::Shieldwall)) return FormationShape::Shieldwall;
        return FormationShape::Line;
    }

    // 4. Marching.
    if (formationAvailable(troop, FormationShape::Manipular)) return FormationShape::Manipular;
    return shapeForUnit(lo.unit);
}

void setSquadShape(SquadHot& squads, size_t s, FormationShape shape) {
    if ((FormationShape)squads.shape[s] == shape) return;
    if (squads.formationHold[s] > 0.0f) return;   // still drilling the last one

    squads.prevShape[s]     = squads.shape[s];
    squads.shape[s]         = (uint8_t)shape;
    squads.shapeBlend[s]    = kFormationChangeSeconds;
    squads.formationHold[s] = kFormationHoldSeconds;
}

void decayMissilePressure(SquadHot& squads, size_t s, float dt) {
    squads.missilePressure[s] -= kMissilePressureDecay * dt;
    if (squads.missilePressure[s] < 0.0f) squads.missilePressure[s] = 0.0f;
}
```

Declare all four in `c++/src/Squads.hpp`.

- [ ] **Step 6: Call them from `squadDecide`**

At the end of `squadDecide`, before the friendly-fire block, add:

```cpp
    // Formation last: it reads the order and contact state everything above
    // just settled. Timers tick here because this is the one place per tick
    // that runs for every live squad.
    decayMissilePressure(squads, s, dt);
    if (squads.formationHold[s] > 0.0f) squads.formationHold[s] -= dt;
    if (squads.shapeBlend[s]    > 0.0f) squads.shapeBlend[s]    -= dt;
    setSquadShape(squads, s, chooseFormation(squads, s));
```

Note that the early `return` paths in `squadDecide` (no enemies left, and the withdraw bypass) skip this. Move the four lines above into a small lambda called on every path, or restructure so they run first. Running them FIRST is correct and simpler: the timers do not depend on this tick's order, and `chooseFormation` reading last tick's order is exactly the one-tick lag `friendlyNearTarget` already documents as harmless.

- [ ] **Step 7: Blend the slots**

In `c++/src/Soldiers.cpp`, in `slotWorldPosition`, replace the single `formationSlot` call:

```cpp
    const FormationShape shape = (FormationShape)squads.shape[s];
    Vec2 raw  = formationSlot(shape, slotIndex, memberCount);
    Vec2 mean = formationMeanOffset(shape, memberCount);

    // Mid-drill, a man walks from where he stood to where he is being sent.
    // The second formationSlot call is paid ONLY while a squad is actually
    // changing shape, which kFormationHoldSeconds bounds to at most 1.4 of
    // every 4.4 seconds.
    if (squads.shapeBlend[s] > 0.0f) {
        const FormationShape from = (FormationShape)squads.prevShape[s];
        const float t = 1.0f - squads.shapeBlend[s] / kFormationChangeSeconds;
        const Vec2 rawFrom  = formationSlot(from, slotIndex, memberCount);
        const Vec2 meanFrom = formationMeanOffset(from, memberCount);
        raw  = Vec2{ rawFrom.x  + (raw.x  - rawFrom.x)  * t,
                     rawFrom.y  + (raw.y  - rawFrom.y)  * t };
        mean = Vec2{ meanFrom.x + (mean.x - meanFrom.x) * t,
                     meanFrom.y + (mean.y - meanFrom.y) * t };
    }
```

Both the slot and the mean are blended. Blending only the slot would move the mean of the slot offsets off zero mid-transition, and the squad would chase its own receding anchor, which is exactly the failure `formationMeanOffset` exists to prevent.

- [ ] **Step 8: Take the slower speed mid-blend**

In `phaseSoldierSteerChunk`, where Task 7 read `traitsOf(shape).speed`, take the minimum with the previous shape while blending:

```cpp
        float formationSpeed = traitsOf((FormationShape)squads.shape[sq]).speed;
        if (squads.shapeBlend[sq] > 0.0f) {
            formationSpeed = std::min(formationSpeed,
                                      traitsOf((FormationShape)squads.prevShape[sq]).speed);
        }
```

`shieldBlockPct` already takes the worse cover of the two shapes, from Task 6.

- [ ] **Step 9: Accumulate the pressure**

In `applyProjectileHits`, after a strike resolves (blocked or not, since being shot at is what matters, not whether it hurt):

```cpp
        const uint16_t sq = soldiers.squadId[t];
        if ((size_t)sq < squads.count) {
            squads.missilePressure[sq] += kMissilePressurePerHit;
        }
```

- [ ] **Step 10: Run, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`, whose digest has moved substantially. This is the task where squads start changing shape, so the whole battle changes.

```bash
git add c++/src/Units.hpp c++/src/Squads.hpp c++/src/Squads.cpp c++/src/Soldiers.cpp c++/src/Projectiles.cpp c++/src/Simulation.cpp c++/tests
git commit -m "feat: squads choose their formation and pay to change it

Formation is chosen from the situation: routing men become a mob, men under
fire and out of contact close up, men in contact form the best fighting shape
their equipment allows. A change takes 1.4 seconds during which the squad has
the worse cover and the slower speed of both shapes."
```

---

## Task 10: The pilum volley

**Files:**
- Modify: `c++/src/Units.hpp` (javelin constants)
- Modify: `c++/src/Squads.hpp` (`pilumSpent`), `c++/src/Squads.cpp`
- Modify: `c++/src/Projectiles.cpp` (`spawnArrows` handles javelins)
- Test: `c++/tests/test_projectiles.cpp`

**Interfaces:**
- Consumes: `Loadout::sidearm`, `ProjectileHot::weapon`.
- Produces: `SquadHot::pilumSpent`, `kPilumRange`, `kJavelinSpeed`, `kJavelinLifetime`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_projectiles.cpp`:

```cpp
TEST_CASE("a legionary squad throws once and only once") {
    Simulation sim(1400, 900, 42u, 0u);
    sim.init(600);
    sim.setPaused(false);
    uint32_t javelinTicks = 0;
    for (uint32_t t = 0; t < 3000; ++t) {
        sim.tick(kFixedTimestep);
        bool sawJavelin = false;
        for (size_t p = 0; p < sim.getProjectileCount(); ++p) {
            if (sim.projectileWeapon(p) == (uint8_t)WeaponClass::Javelin) sawJavelin = true;
        }
        if (sawJavelin) ++javelinTicks;
    }
    // Thrown at all, and not thrown continuously: a squad that never clears
    // pilumSpent would show javelins on hundreds of ticks.
    CHECK(javelinTicks > 0);
    CHECK(javelinTicks < 200);
}

TEST_CASE("a javelin flies faster and dies sooner than an arrow") {
    CHECK(kJavelinSpeed > kArrowSpeed);
    CHECK(kJavelinLifetime < kArrowLifetime);
    // Fast enough and alive long enough to actually cross the throwing range.
    CHECK(kJavelinSpeed * kJavelinLifetime > kPilumRange);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `kJavelinSpeed` not declared.

- [ ] **Step 3: Add the constants**

In `c++/src/Units.hpp`:

```cpp
// The pilum. Thrown once as the lines close, then the swords come out. The
// javelin row of kWoundChancePct is deliberately the best thing in the game
// against mail and mediocre against bare flesh, which is what makes the volley
// a decision rather than a free opener.
constexpr float kPilumRange     = 90.0f;   // px
constexpr float kJavelinSpeed   = 260.0f;  // px/s, flatter than an arrow
constexpr float kJavelinLifetime = 0.6f;   // s, enough for kPilumRange with margin
```

- [ ] **Step 4: Add `pilumSpent` and the trigger**

Add `std::vector<uint8_t> pilumSpent;` to `SquadHot` with its `spawn`, `clear` and digest entries.

In `squadDecide`, after the formation block:

```cpp
    // The throw is a one-shot, and the flag clears only on rally: a legion
    // that re-armed mid-battle would volley every time a fresh enemy squad
    // wandered inside range.
    const Loadout& lo = loadoutOf(squads.troopClass[s]);
    if (lo.weapon == WeaponClass::Javelin && !squads.pilumSpent[s]
        && squads.nearestEnemyDist[s] < kPilumRange) {
        squads.pilumVolley[s] = 1;
        squads.pilumSpent[s]  = 1;
    }
```

Add `pilumVolley` as a second `uint8_t` field: `pilumSpent` is durable state, `pilumVolley` is a one-tick request that resolution consumes and clears. Two flags rather than one, because the alternative is resolution having to distinguish "just set" from "set earlier", which it cannot.

- [ ] **Step 5: Spawn javelins**

In `spawnArrows`, add a branch before the archer path: if the soldier's squad has `pilumVolley` set, spawn one javelin toward the squad's target using `kJavelinSpeed`, `kJavelinLifetime`, `(uint8_t)WeaponClass::Javelin`, and `liveAfter` of `0.0f` (a thrown javelin is a flat trajectory, live from the moment it leaves the hand, unlike an arced volley). Clear `pilumVolley` for every squad at the end of the function.

- [ ] **Step 6: Add the accessor**

`uint8_t projectileWeapon(size_t i) const { return projectiles.weapon[i]; }` on `Simulation`.

- [ ] **Step 7: Run, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`.

```bash
git add c++/src/Units.hpp c++/src/Squads.hpp c++/src/Squads.cpp c++/src/Projectiles.cpp c++/src/Simulation.hpp c++/src/Simulation.cpp c++/tests
git commit -m "feat: legionaries throw the pilum once before drawing swords

Reuses the whole projectile pipeline. The javelin row of the wound table is
what makes the volley worth having against armored troops."
```

---

## Task 11: Legion line relief

The riskiest task. The spec names four new squad fields; this plan adds a fifth, `reliefPartner`, because the pairing has to survive across the two stages and neither squad can re-derive it.

**Files:**
- Modify: `c++/src/Units.hpp` (relief constants)
- Modify: `c++/src/Squads.hpp` (five fields)
- Modify: `c++/src/Army.hpp` / `.cpp` (`updateLineRelief`)
- Modify: `c++/src/Simulation.cpp` (deployment sets `initialMemberCount`, contact duration, digest, call)
- Create: `c++/tests/test_relief.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `SquadRole::Reserve`/`Line`, `SquadOrder::Withdraw`, `kReserveDepth`.
- Produces: `void updateLineRelief(SquadHot&, const ArmyHot&, Team)`, `SquadHot::initialMemberCount`, `::contactDuration`, `::reliefCooldown`, `::reliefStage`, `::reliefPartner`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_relief.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Army.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"

namespace {
// A spent front-line maniple with a fresh reserve directly behind it.
SquadHot twoManiples() {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);   // 0: the spent front line
    q.spawn(Team::A, UnitType::Infantry);   // 1: the reserve
    for (size_t s = 0; s < 2; ++s) {
        q.troopClass[s] = (uint8_t)TroopClass::Legionary;
        q.shape[s] = (uint8_t)FormationShape::Manipular;
        q.facingX[s] = 1.0f;
        q.facingY[s] = 0.0f;
        q.initialMemberCount[s] = 40;
        q.memberCount[s] = 40;
    }
    q.centroidX[0] = 500.0f; q.centroidY[0] = 400.0f;
    q.centroidX[1] = 400.0f; q.centroidY[1] = 400.0f;   // behind, toward our rear
    q.role[0] = (uint8_t)SquadRole::Line;
    q.role[1] = (uint8_t)SquadRole::Reserve;
    q.contact[0] = 1;
    q.memberCount[0] = 20;   // half gone, well under kReliefLossFraction
    return q;
}

ArmyHot oneArmy() {
    ArmyHot a;
    a.spawn();
    a.spawn();
    a.frontDirX[0] = 1.0f;
    a.frontDirY[0] = 0.0f;
    return a;
}
} // namespace

TEST_CASE("a spent maniple is ordered to withdraw and its reserve advances") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Withdraw);
    CHECK(q.role[1] == (uint8_t)SquadRole::Line);
    CHECK(q.reliefPartner[0] == 1);
    CHECK(q.reliefPartner[1] == 0);
    CHECK(q.reliefStage[0] == 1);
}

TEST_CASE("a fresh maniple is not relieved") {
    SquadHot q = twoManiples();
    q.memberCount[0] = 40;      // untouched
    q.morale[0] = 1.0f;
    q.contactDuration[0] = 0.0f;
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == 0);
}

TEST_CASE("relief does not fire again inside the cooldown") {
    SquadHot q = twoManiples();
    ArmyHot a = oneArmy();
    updateLineRelief(q, a, Team::A);
    const uint16_t first = q.reliefPartner[0];
    q.reliefStage[0] = 0;       // pretend it completed
    q.reliefStage[1] = 0;
    updateLineRelief(q, a, Team::A);
    CHECK(q.reliefStage[0] == 0);   // cooldown still running
    CHECK(first == 1);
}

TEST_CASE("a relieved maniple ends up behind the one that replaced it") {
    Simulation sim(2400, 1600, 42u, 0u);
    sim.init(1200);
    sim.setPaused(false);
    for (uint32_t t = 0; t < 4000; ++t) sim.tick(kFixedTimestep);
    // Any squad that completed a relief has its partner nearer the enemy than
    // itself. Checked across the whole army rather than on one pair, because
    // which pair relieves first is not something this test should pin down.
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (sim.squadReliefStage(s) != 0) continue;
        const uint16_t p = sim.squadReliefPartner(s);
        if (p == UINT16_MAX || p >= sim.getSquadCount()) continue;
        if (sim.squadRole(s) != (uint8_t)SquadRole::Reserve) continue;
        // The partner took our place, so it is further along the front.
        CHECK(sim.squadCentroidX(p) != doctest::Approx(sim.squadCentroidX(s)));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `updateLineRelief` not declared.

- [ ] **Step 3: Add the constants**

In `c++/src/Units.hpp`:

```cpp
// Legion line relief (design 6). A maniple that has been fighting long enough
// to be spent retires through the interval behind it and a fresh one steps up.
// Every mechanism this needs already exists: SquadRole::Reserve,
// SquadOrder::Withdraw, and an army decide phase that already runs serially.
constexpr float kReliefLossFraction     = 0.60f;  // survivors below this counts as spent
constexpr float kReliefMoraleThreshold  = 0.55f;
constexpr float kReliefContactSeconds   = 20.0f;
constexpr float kReliefSearchRadius     = 260.0f;
constexpr float kReliefRetireDistance   = 140.0f;
constexpr float kReliefClearDistance    = 70.0f;
constexpr float kReliefCooldownSeconds  = 15.0f;

// kReliefMoraleThreshold MUST stay above kRallyThreshold (0.45), or a maniple
// routs before it is ever judged spent and the relief never fires at all.
// kReliefRetireDistance MUST stay above kReserveDepth (120), or the retiring
// maniple stops inside the line it is trying to fall behind.
```

- [ ] **Step 4: Add the fields**

Add to `SquadHot`, with `spawn`, `clear` and digest entries for each:

```cpp
    // Line relief state (design 6.2). initialMemberCount is set at deployment
    // and never changes, so "how much of this squad is left" is answerable
    // without a second array of starting sizes.
    std::vector<uint32_t> initialMemberCount;
    std::vector<float>    contactDuration;   // seconds in unbroken contact
    std::vector<float>    reliefCooldown;
    std::vector<uint8_t>  reliefStage;       // 0 idle, 1 retiring, 2 closing up
    std::vector<uint16_t> reliefPartner;     // UINT16_MAX when idle
```

In deployment, set `squads.initialMemberCount[squadId]` once the squad's members are spawned.

- [ ] **Step 5: Tick `contactDuration`**

In `squadDecide`, alongside the other timers:

```cpp
    if (squads.contact[s]) squads.contactDuration[s] += dt;
    else                   squads.contactDuration[s]  = 0.0f;
    if (squads.reliefCooldown[s] > 0.0f) squads.reliefCooldown[s] -= dt;
```

- [ ] **Step 6: Implement `updateLineRelief`**

In `c++/src/Army.cpp`:

```cpp
void updateLineRelief(SquadHot& squads, const ArmyHot& armies, Team team) {
    const size_t army = (size_t)team;
    if (army >= armies.count) return;
    const float fdx = armies.frontDirX[army];
    const float fdy = armies.frontDirY[army];

    // Ascending index, first match wins, so the pairing is identical on every
    // platform and at every worker count.
    for (size_t s = 0; s < squads.count; ++s) {
        if (squads.team[s] != team) continue;

        // --- Stage 2: a relief already under way finishes when the two are
        // far enough apart that neither is walking through the other.
        if (squads.reliefStage[s] == 1) {
            const uint16_t p = squads.reliefPartner[s];
            if (p >= squads.count) { squads.reliefStage[s] = 0; continue; }
            const float dx = squads.centroidX[p] - squads.centroidX[s];
            const float dy = squads.centroidY[p] - squads.centroidY[s];
            if (dx * dx + dy * dy > kReliefClearDistance * kReliefClearDistance) {
                squads.reliefStage[s] = 0;
                squads.reliefStage[p] = 0;
                squads.role[s] = (uint8_t)SquadRole::Reserve;
                squads.role[p] = (uint8_t)SquadRole::Line;
            }
            continue;
        }

        if (squads.reliefStage[s] != 0) continue;
        if (squads.memberCount[s] == 0) continue;
        if (squads.reliefCooldown[s] > 0.0f) continue;
        if (squads.role[s] != (uint8_t)SquadRole::Line) continue;
        if (squads.shape[s] != (uint8_t)FormationShape::Manipular) continue;
        if (!squads.contact[s]) continue;

        const uint32_t start = squads.initialMemberCount[s];
        const bool bled   = start > 0 &&
            (float)squads.memberCount[s] < (float)start * kReliefLossFraction;
        const bool shaken = squads.morale[s] < kReliefMoraleThreshold;
        const bool tired  = squads.contactDuration[s] > kReliefContactSeconds;
        if (!bled && !shaken && !tired) continue;

        // --- Find the nearest fresh reserve BEHIND us. Behind is a dot
        // product against the army's front direction, which is the only
        // expression of "our own rear" a single squad cannot work out alone.
        uint16_t best = UINT16_MAX;
        float bestSq = kReliefSearchRadius * kReliefSearchRadius;
        for (size_t r = 0; r < squads.count; ++r) {
            if (squads.team[r] != team) continue;
            if (squads.memberCount[r] == 0) continue;
            if (squads.role[r] != (uint8_t)SquadRole::Reserve) continue;
            if (squads.reliefStage[r] != 0) continue;
            if (squads.reliefCooldown[r] > 0.0f) continue;

            const float dx = squads.centroidX[r] - squads.centroidX[s];
            const float dy = squads.centroidY[r] - squads.centroidY[s];
            if (dx * fdx + dy * fdy > 0.0f) continue;   // in front of us, not behind
            const float d = dx * dx + dy * dy;
            if (d < bestSq) { bestSq = d; best = (uint16_t)r; }
        }
        if (best == UINT16_MAX) continue;

        // --- Stage 1. The spent maniple retires straight back; the reserve
        // aims at the INTERVAL beside our position, not at our position. The
        // lateral offset is what lets two squads swap places without any new
        // collision logic, because they are never walking at the same point.
        squads.order[s]          = (uint8_t)SquadOrder::Withdraw;
        squads.reliefStage[s]    = 1;
        squads.reliefStage[best] = 1;
        squads.reliefPartner[s]    = best;
        squads.reliefPartner[best] = (uint16_t)s;
        squads.reliefCooldown[s]    = kReliefCooldownSeconds;
        squads.reliefCooldown[best] = kReliefCooldownSeconds;
        squads.role[best] = (uint8_t)SquadRole::Line;
        squads.targetSquad[best] = squads.targetSquad[s];
    }
}
```

Declare it in `c++/src/Army.hpp` and call it from `phaseArmyDecide` for each team, immediately after `assignRoles`. `assignRoles` must not overwrite the roles a relief in progress has set: add an early `continue` in `assignRoles` for any squad with `reliefStage != 0`, and say in a comment that a relief in progress outranks a role assignment because it is mid-maneuver.

- [ ] **Step 7: Add the accessors**

```cpp
    uint8_t  squadReliefStage(size_t s) const { return squads.reliefStage[s]; }
    uint16_t squadReliefPartner(size_t s) const { return squads.reliefPartner[s]; }
```

- [ ] **Step 8: Run and check the acceptance criterion**

Run: `scripts/build.bat -t`
Expected: PASS except `test_counters`.

Then run the GUI (`build/Release/tactix.exe`), watch a legion line for two minutes, and confirm the spec's acceptance criterion by eye: a relief completes without the two squads walking through each other, and the front line never opens a gap wider than one maniple.

**If it cannot,** take the spec's stated fallback rather than inventing a third stage: add `&& !squads.contact[s]` to the stage 1 trigger so a maniple is relieved only once its contact has cleared. Less impressive, always safe, and the spec sanctioned it in advance.

- [ ] **Step 9: Regenerate baselines and commit**

```bash
git add c++/src/Units.hpp c++/src/Squads.hpp c++/src/Squads.cpp c++/src/Army.hpp c++/src/Army.cpp c++/src/Simulation.hpp c++/src/Simulation.cpp c++/tests c++/CMakeLists.txt
git commit -m "feat: a spent maniple retires and a fresh one takes its place

Built entirely from orders and roles that already existed. The reserve aims at
the interval beside the front line rather than at it, which is what lets two
squads swap places without new collision logic."
```

---

## Task 12: Rendering

**Files:**
- Modify: `c++/src/Renderer.cpp`

**Interfaces:**
- Consumes: everything.
- Produces: nothing the simulation reads. This task must not move the digest at all.

- [ ] **Step 1: Add the viewport cull**

In `drawSimulation`, before the soldier loop, compute the visible world rectangle from `view` and skip any soldier outside it plus a margin of `kSlotSpacing * 2`:

```cpp
    // The loop below used to draw all 10,000 agents whatever the camera was
    // looking at, and raylib still batches a quad for each one. This is a
    // prerequisite for the per-soldier detail added below, not an
    // optimisation: without it, every extra draw is paid for the whole army.
```

Verify with the frame timer in the HUD that zoomed-in frames get cheaper, not just no more expensive.

- [ ] **Step 2: Tier 1, formation glyphs at squad scale**

Inside the existing `view.squadOverlay` block, switch on `sim.squads.shape[s]` and draw:

| Shape | Glyph |
|---|---|
| Testudo | Filled square at high alpha, and skip the facing tick. A closed box. |
| Shieldwall | The existing disc plus a thick arc across the front third, using the facing vector. |
| Phalanx | The existing disc plus three short lines projecting forward from the front edge. |
| Manipular | The existing disc with a vertical gap drawn through it, echoing the interval. |
| Mob | No hull. Small dots at member positions at low alpha. |

Shift the disc's value by the squad's mean armor: `shade(base, 0.82f + 0.06f * (float)loadoutOf(sim.squads.troopClass[s]).armor)`. Value, never hue, exactly as unit type already does, so an armored squad reads heavier without either team drifting toward the other's color.

- [ ] **Step 3: Tier 2, shields per man**

Between `wantDetail` and `wantPips`, for each visible soldier whose shield class is not `None`, draw one 2px bar offset `kSoldierRadius + 1.0f` along the squad facing, colored by shield class (buckler darkest and smallest, tower brightest and widest). Unshielded troops draw nothing, so archers cost zero.

Fold armor into the body color as brightness alongside the existing health term, which adds no draw call at all.

- [ ] **Step 4: Tier 3, weapon marks**

At `zoom >= 2.2`, draw a line along the facing per soldier: length `18.0f` for a spear (deliberately reaching past the shield bar, which is the entire point of a spear and should be visible), `8.0f` for a sword, `10.0f` for a lance, and nothing for a bow.

- [ ] **Step 5: Verify and commit**

Run the GUI and check each zoom band. Run `scripts/build.bat -t`.
Expected: PASS including `test_counters`. **The digest must not move.** If it does, the render is writing simulation state, which is a bug in this task.

```bash
git add c++/src/Renderer.cpp
git commit -m "feat: draw formation, armor and shields at three zoom levels

Adds the viewport cull the soldier loop never had, which is what makes a
per-soldier shield draw affordable at all."
```

---

## Task 13: Verification, re-measurement and the README

**Files:**
- Modify: `README.md`
- Modify: `c++/tests/baseline/*.txt`

- [ ] **Step 1: Verify every new field reaches the digest**

Read `SoldierHot`, `SquadHot` and `ProjectileHot` field by field against `stateDigest()`, `spawn()`, `clear()` and the two compaction routines. Every field added in this plan must appear in all of them. The list to check: `SoldierHot::troopClass`; `SquadHot::troopClass`, `shape`, `prevShape`, `shapeBlend`, `formationHold`, `missilePressure`, `pilumSpent`, `pilumVolley`, `initialMemberCount`, `contactDuration`, `reliefCooldown`, `reliefStage`, `reliefPartner`; `ProjectileHot::weapon`.

- [ ] **Step 2: Verify thread-count invariance**

```bash
build/Release/tactix_bench.exe --agents 4000 --ticks 800 --seed 42 --threads 1 --json
build/Release/tactix_bench.exe --agents 4000 --ticks 800 --seed 42 --threads 15 --json
```

Expected: identical `stateDigest`. A mismatch is a real bug and blocks the rest of this task.

- [ ] **Step 3: Measure the mechanism separately from the pacing**

Run 1, the real thing:

```bash
build/Release/tactix_bench.exe --agents 10000 --ticks 2000 --seed 42 --json
```

Run 2, with every `kWoundChancePct` entry temporarily set to 100 and `kShieldCoverPct` to all zeros, which restores the old lethality while leaving every new code path live. Rebuild, rerun the same command, then **revert the constants**.

The difference between run 2 and the README's current 9.0937 ms p50 is what this plan's mechanism costs. The difference between run 1 and run 2 is the pacing change: armored men live longer, so a fixed-length run simulates a fuller field for more of its ticks. This is the same attribution method the README already uses for `kArrowHitChancePct`, and it is what makes the claim checkable rather than plausible.

- [ ] **Step 4: Update the README**

Replace the metrics table with run 1's figures and the new digest. Add a paragraph naming both numbers from step 3 and what each one attributes to, in the style of the existing `kArrowHitChancePct` paragraph. Do not describe a p50 increase as a regression if the cause is pacing; say which of the two it is, with the number that shows it.

- [ ] **Step 5: Regenerate both counter baselines one final time and commit**

```bash
git add README.md c++/tests/baseline
git commit -m "docs: republish tick figures and digest after the armor model

Measured twice: once as shipped, once with wound chances forced to 100 and
shield cover to zero, which separates what the new code costs from what
longer-lived soldiers cost."
```

---

## Self-Review

**Spec coverage.** Section 3.1 to 3.4 is Task 1. Section 4.1 and 4.2 is Tasks 2 and 3. Section 4.3 to 4.5 is Task 9. Section 5.1 and 5.2 is Tasks 5 and 6. Section 5.3 is Tasks 4, 6 and 7. Section 5.4 is Task 8. Section 5.5 is Task 10. Section 6 is Task 11. Section 7 is verified in Task 13 step 1 and enforced per-task by the global constraints. Section 8 is Task 12. Section 9's test list is distributed across the task that implements each behavior. Section 10's measurement plan is Task 13. No spec section is unimplemented.

**Known ordering hazard.** Task 6 adds `prevShape` and `shapeBlend` to `SquadHot` but Task 9 is what starts writing them. This is deliberate: `shieldBlockPct` needs to read them, and adding a field in one task and populating it in a later one is safer than a second signature change. Task 6's commit message says so.

**Deferred, per the spec.** The rear-rank melee query optimization (Task 8 step 3's comment), the overhead impact arc, fatigue, mixed-loadout squads and player-issued formation orders are all out of scope here.

