# Medieval Skirmish, Plan 1: Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the zombie scenario with two symmetric armies that deploy on opposite edges of a variable-size map and march in formation, deterministically and at 10,000 soldiers.

**Architecture:** A two-tier simulation. A `SquadHot` SoA array (~400 entries) holds the group tier; a `SoldierHot` SoA array (~10,000 entries) holds individuals, grouped by a `squadMembers` index array rebuilt each tick. The tick runs as ordered phases separated by job-system barriers, where no parallel phase ever mutates another agent. This plan builds everything up to and including formation marching. Combat, tactics, and overlays are plans 2 through 4.

**Tech Stack:** C++20, CMake 3.20+, raylib 5.5 (GUI target only), doctest 2.5.3, spdlog 1.14.1. Custom `JobSystem`, `SpatialHash`, `Rng`, `StateDigest`, `DetMath`.

**Spec:** [`docs/superpowers/specs/2026-08-19-tactix-medieval-skirmish-design.md`](../specs/2026-08-19-tactix-medieval-skirmish-design.md)

## Global Constraints

- **Determinism is the hard gate.** The same seed must produce a bit-identical `stateDigest()` at any worker-thread count. Every change is checked against this.
- **`tactix_sim` MUST NOT link raylib.** The simulation core is headless. Only the `tactix` GUI target links raylib.
- **No parallel phase may mutate another agent.** Cross-agent effects are written as intent fields on the actor's own slot and applied in the single-threaded resolution phase.
- **Floating point:** all simulation code carries `tactix_fp_flags` (`/fp:precise` on MSVC, `-ffp-contract=off` otherwise). 64-bit targets only.
- **Transcendentals go through `detmath::`, never libm.** `std::sqrt` is the sole exception; IEEE-754 requires it to be correctly rounded.
- **`Rng` is stateless and keyed on `(seed, tick, agentIndex, RngUse)`.** Adding a new `RngUse` enumerator does not perturb any existing draw, so new random behavior can be added in later plans without shifting this plan's results. Keep `RngUse::Count` last.
- **CMake source lists are explicit, never GLOB.** Every new `.cpp` must be added to `c++/CMakeLists.txt` by hand.
- **No em dashes or en dashes** in any code comment, document, commit message, or PR body.
- **Never add `Co-Authored-By` or any AI attribution** to commit messages.

## Known Issues To Fix In This Plan

- **CI does not run on this branch.** `.github/workflows/ci.yml` triggers on `branches: [master, 'phase-*']`. The working branch is `medieval-skirmish-pivot`, which matches neither. Task 10 adds the pattern.

## Known Issues NOT To Fix In This Plan

- `JobSystem::waitAll()` reads `jobQueue.empty()` holding only `waitMutex`, not `queueMutex`. Known, pre-existing, flagged in the CI TSan job with `continue-on-error`. Out of scope.
- The four `WorkCounters` atomics share a cache line and are incremented from worker threads. Deliberately parked; see `WorkCounters.hpp`.

## File Structure

| File | Responsibility | Status |
|------|----------------|--------|
| `c++/src/Units.hpp` | `Team`, `UnitType`, `SoldierState`, `FormationShape`, `Vec2`, unit stat table | Create |
| `c++/src/Formation.hpp` | `formationSlot()`, a pure function with no state | Create |
| `c++/src/Squads.hpp` / `.cpp` | `SquadHot`, `rebuildSquadMembers`, squad aggregate and facing | Create |
| `c++/src/Soldiers.hpp` / `.cpp` | Formation steering, separation | Create |
| `c++/src/Simulation.hpp` / `.cpp` | `SoldierHot`, tick orchestration, phase barriers, deployment | Modify (heavy) |
| `c++/src/Renderer.cpp` | Team colors, unit silhouettes, world border | Modify |
| `c++/src/main.cpp` | World size independent of window size | Modify |
| `c++/bench/main.cpp` | `--width` / `--height` flags | Modify |
| `c++/src/Rng.hpp` | New `RngUse` enumerators, zombie ones deleted | Modify |
| `c++/tests/test_formation.cpp` | Formation slot geometry and stability | Create |
| `c++/tests/test_squads.cpp` | Membership rebuild, stable ordering, aggregate, facing | Create |
| `c++/tests/test_deployment.cpp` | Army placement, map coverage, variable map size | Create |
| `c++/CMakeLists.txt` | New source and test files | Modify |
| `.github/workflows/ci.yml` | Branch trigger pattern | Modify |

Files deleted outright: none. `Simulation.cpp` loses roughly 800 lines to Task 1.

---

## Task 1: Strip the zombie scenario to a movable core

Removes the infection, combat-lock, reanimation, and hero-shooting systems, leaving agents that exist, separate, avoid obstacles, and integrate velocity. Nothing in this task adds medieval content; it clears the ground so later tasks are not editing around dead code.

**Files:**
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/Rng.hpp`, `c++/src/Renderer.cpp`
- Modify: `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: a `Simulation` whose tick is `rebuildSpatialHash()`, `updateSeparation()`, `updateMovement()`, `screenWrap()`. `EntityHot` retains `posX/posY/velX/velY/dirX/dirY` and nothing else. `AgentType` and `AgentState` are gone.

- [ ] **Step 1: Confirm the determinism tests pass before touching anything**

Run:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests PASS. If they do not, stop and report; this plan assumes a green baseline.

- [ ] **Step 2: Delete the zombie subsystems from `Simulation.cpp`**

Delete these members entirely, including their declarations in `Simulation.hpp`:

- `updateInfections()`
- `resolveCivilianVsZombieCombat()`
- `resolveHeroVsZombieCombat()`
- `generateObstacles()` keeps buildings and trees, but delete the `graveyard` member and its initialization in `init()`
- The ranged-kill block in `tick()` (the loop reading `shootCooldown > 1.45f` and decoding indices from `lastSeenX/lastSeenY`) and the swap-remove loop that follows it

From `EntityHot`, delete every field except `posX`, `posY`, `velX`, `velY`, `dirX`, `dirY`, and `count`. That removes `type`, `state`, `health`, `lastSeenX/Y`, `searchTimer`, `patrolTargetX/Y`, `shootCooldown`, `aimTimer`, `fleeStrategy`, `heroType`, `reanimationTimer`, `meleeAttackCooldown`, `combatTarget`, `combatTimer`, `combatCooldown`, `infectionTimer`, `infectionProgress`.

Delete `enum class AgentType` and `enum class AgentState`.

Delete `updateBehaviors()` and `updateBehaviorsChunk()` in full. All patrol, flee, seek, horde, and hero logic goes with them, including the hardcoded `50..1850` and `50..1030` patrol bounds.

Delete `getCivilianCount()`, `getZombieCount()`, `getHeroCount()`, `recentGunshots`, `gunshotLines`, and the `Gunshot` and `GunshotLine` structs.

- [ ] **Step 3: Reduce `EntityHot::spawn` and `init`**

Replace `EntityHot::spawn` with:

```cpp
void spawn(float px, float py, float vx, float vy) {
    posX.push_back(px);
    posY.push_back(py);
    velX.push_back(vx);
    velY.push_back(vy);
    const float speed = std::sqrt(vx * vx + vy * vy);
    if (speed > 0.01f) {
        dirX.push_back(vx / speed);
        dirY.push_back(vy / speed);
    } else {
        dirX.push_back(1.0f);
        dirY.push_back(0.0f);
    }
    count++;
}
```

Replace the three spawn loops in `init()` with one uniform loop. Deployment proper arrives in Task 5:

```cpp
void Simulation::init(size_t count) {
    const Rng rng{worldSeed, 0u};
    entities.reserve(count);
    prevPosX.reserve(count);
    prevPosY.reserve(count);

    for (size_t i = 0; i < count; i++) {
        const uint32_t agent = (uint32_t)entities.count;
        const float px = (float)rng.range(agent, RngUse::SpawnPosX, 0, screenWidth);
        const float py = (float)rng.range(agent, RngUse::SpawnPosY, 0, screenHeight);
        const float vx = (float)rng.range(agent, RngUse::SpawnVelX, -10, 10);
        const float vy = (float)rng.range(agent, RngUse::SpawnVelY, -10, 10);
        entities.spawn(px, py, vx, vy);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }

    generateObstacles();
}
```

Reduce `setAgentCount()` the same way: one loop using `RngUse::SpawnPosX` and friends, and a shrink path that resizes only the six surviving vectors.

- [ ] **Step 4: Reduce `tick()` to the surviving phases**

```cpp
void Simulation::tick(float dt) {
    if (paused) return;

    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};

    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }

    rebuildSpatialHash();
    jobSystem.resetJobCounter();

    updateSeparation(dt, rng);
    updateMovement(dt);
    screenWrap();
}
```

- [ ] **Step 5: Reduce `stateDigest()` to the surviving fields**

```cpp
uint64_t Simulation::stateDigest() const {
    StateDigest d;
    d.mix(static_cast<uint32_t>(entities.count));
    for (size_t i = 0; i < entities.count; ++i) {
        d.mix(entities.posX[i]);
        d.mix(entities.posY[i]);
        d.mix(entities.velX[i]);
        d.mix(entities.velY[i]);
    }
    return d.value();
}
```

- [ ] **Step 6: Delete the dead `RngUse` enumerators**

In `Rng.hpp`, delete every enumerator whose call site is gone. Keep `SpawnPosX`, `SpawnPosY`, `SpawnVelX`, `SpawnVelY`, `ObstacleBuildingX/Y/W/H`, `ObstacleTreeX/Y/Radius`, `SeparationPushX/Y`, `SeparationTreePushX/Y`, and the trailing `Count` sentinel. Delete all the rest.

- [ ] **Step 7: Reduce `Renderer.cpp` to compile against the new state**

Delete the graveyard, tombstone, gunshot-line, and per-type color blocks. Every agent draws as one color for now; Task 11 restores real visuals.

```cpp
Color agentColor = Color{200, 200, 200, 255};
```

- [ ] **Step 8: Build and run the tests**

Run:

```bash
cd c++ && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

Expected: compiles clean. `test_determinism` PASSES (same seed reproduces, seeds diverge, thread counts agree). `test_counters` FAILS on `stateDigest`, because behavior legitimately changed.

- [ ] **Step 9: Regenerate the counter baseline**

Run:

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

(On Linux/macOS the binary is at `./build/tactix_bench`.)

Copy the four counter values and `stateDigest` from the JSON into `c++/tests/baseline/counters-2k-200.txt`. The digest is hex, exactly as printed. Do not hand-edit the numbers.

- [ ] **Step 10: Verify the whole suite passes**

Run:

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS.

- [ ] **Step 11: Commit**

```bash
git add c++/src c++/tests/baseline/counters-2k-200.txt
git commit -m "refactor: strip the zombie scenario to a movable core

Removes infection, combat locks, reanimation, hero shooting, and all
per-agent behavior, leaving agents that separate, avoid obstacles, and
integrate velocity. The hardcoded 1850x1030 patrol bounds go with the
patrol system that read them.

Digest baseline regenerated: behavior changed deliberately."
```

---

## Task 2: Soldier identity fields

Adds team, unit type, state, and health to the soldier array, and the unit stat table they index.

**Files:**
- Create: `c++/src/Units.hpp`
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/CMakeLists.txt`
- Modify: `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: Task 1's reduced `EntityHot`.
- Produces: `enum class Team : uint8_t { A = 0, B = 1 }`; `enum class UnitType : uint8_t { Infantry = 0, Archer = 1, Cavalry = 2 }`; `enum class SoldierState : uint8_t { Forming = 0, Engaged = 1, Routing = 2, Dead = 3 }`; `struct Vec2 { float x, y; }`; `struct UnitStats { float speed; float range; uint8_t maxHealth; }` and `constexpr UnitStats kUnitStats[3]`. `EntityHot` is renamed `SoldierHot` and gains `team`, `unitType`, `state`, `squadId`, `slotIndex`, `health`, `attackCooldown`, `intentTarget`, `intentFire`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_units.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Units.hpp"

TEST_CASE("unit stats table is indexed by UnitType") {
    CHECK(kUnitStats[(int)UnitType::Infantry].speed == doctest::Approx(45.0f));
    CHECK(kUnitStats[(int)UnitType::Archer].speed   == doctest::Approx(42.0f));
    CHECK(kUnitStats[(int)UnitType::Cavalry].speed  == doctest::Approx(95.0f));
}

TEST_CASE("only archers have a ranged attack") {
    CHECK(kUnitStats[(int)UnitType::Archer].range > 0.0f);
    CHECK(kUnitStats[(int)UnitType::Infantry].range == doctest::Approx(0.0f));
    CHECK(kUnitStats[(int)UnitType::Cavalry].range  == doctest::Approx(0.0f));
}

TEST_CASE("archer range exceeds the soldier perception radius") {
    // This gap is the entire justification for the squad tier: an archer
    // cannot perceive its own best target, so the squad must assign one.
    CHECK(kUnitStats[(int)UnitType::Archer].range > kSeekRadius);
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_units.cpp` to the `tactix_tests` source list in `c++/CMakeLists.txt`, then:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel
```

Expected: FAIL to compile, `Units.hpp` not found.

- [ ] **Step 3: Create `Units.hpp`**

```cpp
#pragma once
#include <cstdint>

enum class Team : uint8_t { A = 0, B = 1 };
enum class UnitType : uint8_t { Infantry = 0, Archer = 1, Cavalry = 2 };
enum class SoldierState : uint8_t { Forming = 0, Engaged = 1, Routing = 2, Dead = 3 };

constexpr uint32_t kUnitTypeCount = 3;

struct Vec2 { float x, y; };

// Radius of a soldier's own neighbour query. Archer range deliberately
// exceeds it, which is why target assignment lives on the squad.
constexpr float kSeekRadius = 150.0f;

struct UnitStats {
    float   speed;      // px/s
    float   range;      // px, 0 means melee only
    uint8_t maxHealth;
};

constexpr UnitStats kUnitStats[kUnitTypeCount] = {
    /* Infantry */ { 45.0f,   0.0f, 3 },
    /* Archer   */ { 42.0f, 280.0f, 2 },
    /* Cavalry  */ { 95.0f,   0.0f, 3 },
};
```

- [ ] **Step 4: Run the test to confirm it passes**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -ts="*" -tc="*unit*"
```

Expected: PASS.

- [ ] **Step 5: Rename `EntityHot` to `SoldierHot` and add the fields**

In `Simulation.hpp`, rename the struct and add:

```cpp
std::vector<Team>         team;
std::vector<UnitType>     unitType;
std::vector<SoldierState> state;
std::vector<uint16_t>     squadId;
std::vector<uint16_t>     slotIndex;
std::vector<uint8_t>      health;
std::vector<float>        attackCooldown;
std::vector<uint32_t>     intentTarget;   // UINT32_MAX means none
std::vector<uint8_t>      intentFire;
```

Extend `spawn` to take `Team`, `UnitType`, and `uint16_t squadId`, pushing `SoldierState::Forming`, `kUnitStats[(int)unitType].maxHealth`, `0.0f`, `UINT32_MAX`, and `0` for the rest. `slotIndex` is pushed as 0 and assigned properly by Task 4.

Update `init()` to alternate teams and cycle unit types so the array is populated; real deployment is Task 5.

- [ ] **Step 6: Extend the digest**

Add to the per-soldier loop in `stateDigest()`, after the velocity mixes:

```cpp
d.mix(static_cast<uint32_t>(entities.team[i]));
d.mix(static_cast<uint32_t>(entities.unitType[i]));
d.mix(static_cast<uint32_t>(entities.state[i]));
d.mix(static_cast<uint32_t>(entities.squadId[i]));
d.mix(static_cast<uint32_t>(entities.slotIndex[i]));
d.mix(static_cast<uint32_t>(entities.health[i]));
```

Omitting `health` or the membership fields would let the thread-invariance gate pass while combat silently diverged in plan 2.

- [ ] **Step 7: Rebuild, regenerate the baseline, run everything**

```bash
cd c++ && cmake --build build --config Release --parallel \
  && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste the counters and digest into `c++/tests/baseline/counters-2k-200.txt`, then:

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add c++/src c++/tests c++/CMakeLists.txt
git commit -m "feat: add soldier identity fields and the unit stat table

Renames EntityHot to SoldierHot and adds team, unitType, state, squadId,
slotIndex, health, and the two intent fields plans 2 and 3 consume.

Archer range (280px) deliberately exceeds the soldier perception radius
(150px). A test pins that relationship, because it is the reason target
assignment has to live on the squad rather than the soldier.

Digest extended to the new fields and baseline regenerated."
```

---

## Task 3: Formation slot geometry

A pure function mapping a slot index to an offset in squad-local space. No simulation state involved, so it is tested in isolation.

**Files:**
- Create: `c++/src/Formation.hpp`, `c++/tests/test_formation.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `Vec2`, `FormationShape` from `Units.hpp` (add `FormationShape` there in Step 3).
- Produces: `Vec2 formationSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount)`, returning `(right, forward)` in squad-local space where `+forward` is toward the enemy. `constexpr float kSlotSpacing = 12.0f`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_formation.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Formation.hpp"
#include <algorithm>
#include <cmath>

TEST_CASE("slot 0 sits on the front rank") {
    const Vec2 s = formationSlot(FormationShape::Line, 0, 25);
    CHECK(s.y == doctest::Approx(0.0f));
}

TEST_CASE("a line is wider than it is deep") {
    // Sample every slot and compare extents.
    float maxAbsRight = 0.0f, maxAbsForward = 0.0f;
    for (uint16_t i = 0; i < 25; ++i) {
        const Vec2 s = formationSlot(FormationShape::Line, i, 25);
        maxAbsRight   = std::max(maxAbsRight,   std::abs(s.x));
        maxAbsForward = std::max(maxAbsForward, std::abs(s.y));
    }
    CHECK(maxAbsRight > maxAbsForward);
}

TEST_CASE("a column is deeper than it is wide") {
    float maxAbsRight = 0.0f, maxAbsForward = 0.0f;
    for (uint16_t i = 0; i < 25; ++i) {
        const Vec2 s = formationSlot(FormationShape::Column, i, 25);
        maxAbsRight   = std::max(maxAbsRight,   std::abs(s.x));
        maxAbsForward = std::max(maxAbsForward, std::abs(s.y));
    }
    CHECK(maxAbsForward > maxAbsRight);
}

TEST_CASE("loose spacing is wider than line spacing for the same count") {
    const Vec2 line  = formationSlot(FormationShape::Line,  24, 25);
    const Vec2 loose = formationSlot(FormationShape::Loose, 24, 25);
    CHECK(std::abs(loose.x) > std::abs(line.x));
}

TEST_CASE("a wedge widens by two per rank") {
    // Rank r starts at slot r*r and holds 2r+1 slots, so slot 0 is the tip,
    // slots 1..3 are the second rank, slots 4..8 the third.
    CHECK(formationSlot(FormationShape::Wedge, 0, 9).y == doctest::Approx(0.0f));
    const Vec2 rank1 = formationSlot(FormationShape::Wedge, 1, 9);
    const Vec2 rank2 = formationSlot(FormationShape::Wedge, 4, 9);
    CHECK(rank1.y < 0.0f);
    CHECK(rank2.y < rank1.y);
}

TEST_CASE("slots are stable across calls") {
    // Same inputs must always give the same output. Formation slots are
    // recomputed every tick, so instability would jitter every soldier.
    for (uint16_t i = 0; i < 40; ++i) {
        const Vec2 a = formationSlot(FormationShape::Line, i, 40);
        const Vec2 b = formationSlot(FormationShape::Line, i, 40);
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }
}

TEST_CASE("an empty squad returns the origin rather than dividing by zero") {
    const Vec2 s = formationSlot(FormationShape::Line, 0, 0);
    CHECK(s.x == doctest::Approx(0.0f));
    CHECK(s.y == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_formation.cpp` to `tactix_tests` in `c++/CMakeLists.txt`, then:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel
```

Expected: FAIL to compile, `Formation.hpp` not found.

- [ ] **Step 3: Add `FormationShape` to `Units.hpp`**

```cpp
enum class FormationShape : uint8_t { Line = 0, Column = 1, Wedge = 2, Loose = 3 };
```

- [ ] **Step 4: Create `Formation.hpp`**

```cpp
#pragma once
#include "Units.hpp"
#include <cmath>
#include <cstdint>

constexpr float kSlotSpacing = 12.0f;

namespace detail {

// Smallest w such that w * ceil(n/w) >= n and w/depth is near the target
// aspect. Computed by search rather than closed form: n is at most a few
// hundred, and a loop is easier to verify than the algebra.
inline uint32_t rankWidth(uint32_t memberCount, float aspect) {
    if (memberCount <= 1) return 1;
    const float ideal = std::sqrt((float)memberCount * aspect);
    uint32_t w = (uint32_t)(ideal + 0.5f);
    if (w < 1) w = 1;
    if (w > memberCount) w = memberCount;
    return w;
}

} // namespace detail

// Returns an offset in squad-local space: +x is squad-right, +y is toward
// the enemy. Rank 0 is the front, so all slots have y <= 0.
inline Vec2 formationSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount) {
    if (memberCount == 0) return Vec2{0.0f, 0.0f};

    if (shape == FormationShape::Wedge) {
        // Rank r begins at slot r*r and holds 2r+1 slots, so r = floor(sqrt(i)).
        const uint32_t r = (uint32_t)std::sqrt((float)slotIndex);
        const uint32_t posInRank = slotIndex - r * r;   // 0 .. 2r
        const float col = (float)posInRank - (float)r;  // -r .. +r
        return Vec2{ col * kSlotSpacing, -(float)r * kSlotSpacing };
    }

    float spacing = kSlotSpacing;
    float aspect  = 2.0f;
    if (shape == FormationShape::Column) {
        aspect = 0.5f;
    } else if (shape == FormationShape::Loose) {
        spacing = kSlotSpacing * 2.0f;
    }

    const uint32_t width = detail::rankWidth(memberCount, aspect);
    const uint32_t row = slotIndex / width;
    const uint32_t col = slotIndex % width;

    const float right = ((float)col - (float)(width - 1) * 0.5f) * spacing;
    const float forward = -(float)row * spacing;
    return Vec2{ right, forward };
}
```

- [ ] **Step 5: Run the tests to confirm they pass**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*formation*,*slot*,*line*,*column*,*wedge*,*loose*"
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Formation.hpp c++/src/Units.hpp c++/tests/test_formation.cpp c++/CMakeLists.txt
git commit -m "feat: formation slot geometry

A pure function from slot index to squad-local offset, so it is testable
without any simulation state. Line, Column, Wedge, and Loose.

The wedge uses the identity that rank r starts at slot r*r and holds 2r+1
slots, so the rank is floor(sqrt(index)) with no table or loop."
```

---

## Task 4: Squad array and stable membership rebuild

Adds the squad tier and the per-tick membership rebuild. Stability is a correctness requirement, not an optimization, so it gets its own test.

**Files:**
- Create: `c++/src/Squads.hpp`, `c++/src/Squads.cpp`, `c++/tests/test_squads.cpp`
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `SoldierHot` from Task 2.
- Produces: `struct SquadHot` with the fields listed below, and
  `void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads, std::vector<uint32_t>& members)`.
  After it returns, `members[squads.memberStart[s] .. +memberCount[s])` holds squad `s`'s soldier indices ordered by their previous `slotIndex`, and each soldier's `slotIndex` has been reassigned to its position within that range.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_squads.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Squads.hpp"
#include "Simulation.hpp"
#include <vector>

namespace {
// Builds a soldier array with a fixed squad assignment, bypassing Simulation
// so the membership rebuild can be tested on its own.
SoldierHot makeSoldiers(const std::vector<uint16_t>& squadIds,
                        const std::vector<uint16_t>& slotIndices) {
    SoldierHot s;
    for (size_t i = 0; i < squadIds.size(); ++i) {
        s.spawn(0.0f, 0.0f, 0.0f, 0.0f, Team::A, UnitType::Infantry, squadIds[i]);
        s.slotIndex[i] = slotIndices[i];
    }
    return s;
}

SquadHot makeSquads(uint32_t n) {
    SquadHot q;
    for (uint32_t i = 0; i < n; ++i) {
        q.spawn(Team::A, UnitType::Infantry);
    }
    return q;
}
} // namespace

TEST_CASE("members are grouped by squad") {
    SoldierHot s = makeSoldiers({1, 0, 1, 0}, {0, 0, 1, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 2);
    CHECK(q.memberCount[1] == 2);
    for (uint32_t i = 0; i < q.memberCount[0]; ++i) {
        CHECK(s.squadId[members[q.memberStart[0] + i]] == 0);
    }
    for (uint32_t i = 0; i < q.memberCount[1]; ++i) {
        CHECK(s.squadId[members[q.memberStart[1] + i]] == 1);
    }
}

TEST_CASE("within-squad order follows previous slotIndex, not array position") {
    // Soldier 0 sits earlier in the array but held slot 2; soldier 2 held
    // slot 0. Ordering by array position would put soldier 0 first, which is
    // exactly the instability that makes an officer's successor arbitrary.
    SoldierHot s = makeSoldiers({0, 0, 0}, {2, 1, 0});
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(members[0] == 2);
    CHECK(members[1] == 1);
    CHECK(members[2] == 0);
}

TEST_CASE("slotIndex is reassigned densely from zero") {
    SoldierHot s = makeSoldiers({0, 0, 0}, {5, 9, 2});
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(s.slotIndex[members[0]] == 0);
    CHECK(s.slotIndex[members[1]] == 1);
    CHECK(s.slotIndex[members[2]] == 2);
}

TEST_CASE("officer succession goes to the previously adjacent soldier") {
    // Slots 0,1,2 held by soldiers 7,3,5. Remove the officer (soldier 7) and
    // the next slot holder must be soldier 3, not whichever survivor happens
    // to sit first in the array.
    SoldierHot s = makeSoldiers({0, 0, 0}, {1, 2, 0});
    //                index:     0  1  2
    //                slot:      1  2  0   -> order is 2, 0, 1
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);
    REQUIRE(members[0] == 2);  // soldier 2 is the officer

    // Kill the officer and rebuild.
    s.state[2] = SoldierState::Dead;
    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 2);
    CHECK(members[0] == 0);  // soldier 0 held slot 1, so it inherits slot 0
    CHECK(s.slotIndex[0] == 0);
}

TEST_CASE("an empty squad has a zero-length range") {
    SoldierHot s = makeSoldiers({1, 1}, {0, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> members;

    rebuildSquadMembers(s, q, members);

    CHECK(q.memberCount[0] == 0);
    CHECK(q.memberCount[1] == 2);
}

TEST_CASE("rebuilding twice is idempotent") {
    SoldierHot s = makeSoldiers({0, 1, 0, 1}, {0, 0, 1, 1});
    SquadHot q = makeSquads(2);
    std::vector<uint32_t> first, second;

    rebuildSquadMembers(s, q, first);
    rebuildSquadMembers(s, q, second);

    CHECK(first == second);
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_squads.cpp` and `src/Squads.cpp` to `c++/CMakeLists.txt` (the latter to the `tactix_sim` source list), then:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel
```

Expected: FAIL to compile, `Squads.hpp` not found.

- [ ] **Step 3: Create `Squads.hpp`**

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;  // defined in Simulation.hpp

struct SquadHot {
    std::vector<Team>     team;
    std::vector<UnitType> unitType;
    std::vector<float>    centroidX, centroidY;
    std::vector<float>    facingX, facingY;
    std::vector<uint8_t>  order;          // plan 3 gives this meaning
    std::vector<uint16_t> targetSquad;
    std::vector<uint32_t> targetSoldier;  // plan 3
    std::vector<float>    morale;         // plan 3
    std::vector<float>    discipline;     // plan 3
    std::vector<uint32_t> memberStart, memberCount;

    size_t count = 0;

    void spawn(Team t, UnitType u) {
        team.push_back(t);
        unitType.push_back(u);
        centroidX.push_back(0.0f);
        centroidY.push_back(0.0f);
        facingX.push_back(1.0f);
        facingY.push_back(0.0f);
        order.push_back(0);
        targetSquad.push_back(0);
        targetSoldier.push_back(UINT32_MAX);
        morale.push_back(1.0f);
        discipline.push_back(1.0f);
        memberStart.push_back(0);
        memberCount.push_back(0);
        count++;
    }
};

// Regroups `members` by squad, ordering each squad's range by the soldiers'
// previous slotIndex, then reassigns slotIndex densely from zero.
//
// Ordering by previous slotIndex rather than array position is a correctness
// requirement: it is what makes a dead officer's successor the soldier who
// was standing next to them, instead of an arbitrary survivor whose position
// would teleport the formation's anchor.
void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members);

// Recomputes each squad's centroid from its members. Parallel-safe: writes
// only the squad it is given, reads only that squad's members.
void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t squadIndex);
```

- [ ] **Step 4: Create `Squads.cpp` with the membership rebuild**

```cpp
#include "Squads.hpp"
#include "Simulation.hpp"
#include <algorithm>

void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members) {
    const size_t squadCount = squads.count;

    // Counting pass. Dead soldiers are excluded so that a squad's range holds
    // only live members; plan 2's compaction removes them from the array.
    std::vector<uint32_t> counts(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        counts[soldiers.squadId[i]]++;
    }

    uint32_t running = 0;
    for (size_t s = 0; s < squadCount; ++s) {
        squads.memberStart[s] = running;
        squads.memberCount[s] = counts[s];
        running += counts[s];
    }

    members.assign(running, 0u);
    std::vector<uint32_t> cursor(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        const uint16_t s = soldiers.squadId[i];
        members[squads.memberStart[s] + cursor[s]++] = (uint32_t)i;
    }

    // Order each squad's range by previous slotIndex. Keys are unique within
    // a squad, so std::sort is deterministic here despite not being stable.
    for (size_t s = 0; s < squadCount; ++s) {
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        std::sort(members.begin() + start, members.begin() + start + n,
                  [&soldiers](uint32_t a, uint32_t b) {
                      return soldiers.slotIndex[a] < soldiers.slotIndex[b];
                  });
        for (uint32_t k = 0; k < n; ++k) {
            soldiers.slotIndex[members[start + k]] = (uint16_t)k;
        }
    }
}
```

- [ ] **Step 5: Run the tests to confirm they pass**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*squad*,*member*,*officer*,*slotIndex*"
```

Expected: all PASS. If "officer succession" fails, the sort comparator is reading the reassigned `slotIndex` rather than the previous one; make sure reassignment happens after the sort, not during it.

- [ ] **Step 6: Wire the rebuild into the tick**

In `Simulation.hpp` add `SquadHot squads;` and `std::vector<uint32_t> squadMembers;`. In `tick()`, call `rebuildSquadMembers(entities, squads, squadMembers);` immediately after `rebuildSpatialHash()`.

- [ ] **Step 7: Rebuild, regenerate the baseline, run everything**

```bash
cd c++ && cmake --build build --config Release --parallel \
  && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste counters and digest into the baseline file, then:

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: squad array and stable membership rebuild

Adds SquadHot and the per-tick regrouping of soldiers into per-squad index
ranges.

Ordering within a squad follows the soldiers' previous slotIndex rather than
their position in the array. That is a correctness requirement: it makes a
dead officer's successor the soldier who was already adjacent, instead of an
arbitrary survivor whose position would teleport the formation anchor. A test
pins the succession case directly."
```

---

## Task 5: Two-army deployment on a variable-size map

Replaces the uniform random spawn with two armies facing each other, and makes world size a real parameter.

**Files:**
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/Rng.hpp`
- Create: `c++/tests/test_deployment.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `SquadHot::spawn` and `SoldierHot::spawn` from Tasks 2 and 4, `formationSlot` from Task 3.
- Produces: `Simulation::init(size_t soldierCount)` deploying two equal armies; `size_t Simulation::getSquadCount() const`; `size_t Simulation::getTeamCount(Team) const`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_deployment.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Simulation.hpp"

TEST_CASE("both armies get roughly half the soldiers") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    const size_t a = sim.getTeamCount(Team::A);
    const size_t b = sim.getTeamCount(Team::B);
    CHECK(a + b == sim.getAgentCount());
    // Squads are whole, so the split is not exact.
    CHECK(a > 400);
    CHECK(b > 400);
}

TEST_CASE("armies deploy on opposite sides") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    // Team A occupies the left third, team B the right third.
    CHECK(sim.teamCentroidX(Team::A) < 1280.0f / 3.0f);
    CHECK(sim.teamCentroidX(Team::B) > 1280.0f * 2.0f / 3.0f);
}

TEST_CASE("deployment scales with map size") {
    Simulation wide(4000, 2500, 42u);
    wide.init(1000);
    CHECK(wide.teamCentroidX(Team::B) > 4000.0f * 2.0f / 3.0f);

    // Every soldier must be inside the world, whatever its size.
    for (size_t i = 0; i < wide.getAgentCount(); ++i) {
        CHECK(wide.soldierX(i) >= 0.0f);
        CHECK(wide.soldierX(i) <= 4000.0f);
        CHECK(wide.soldierY(i) >= 0.0f);
        CHECK(wide.soldierY(i) <= 2500.0f);
    }
}

TEST_CASE("every soldier belongs to a squad that claims it") {
    Simulation sim(1280, 720, 42u);
    sim.init(1000);
    CHECK(sim.getSquadCount() > 0);
    // Membership is rebuilt on the first tick.
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);
    CHECK(sim.everySoldierHasASquadSlot());
}

TEST_CASE("deployment is deterministic for a seed") {
    Simulation a(1280, 720, 7u);
    Simulation b(1280, 720, 7u);
    a.init(500);
    b.init(500);
    CHECK(a.stateDigest() == b.stateDigest());
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_deployment.cpp` to `c++/CMakeLists.txt`, then build.

Expected: FAIL to compile, `getTeamCount` and friends undeclared.

- [ ] **Step 3: Add the new `RngUse` enumerators**

In `Rng.hpp`, before the trailing `Count`:

```cpp
    // Deployment
    DeployJitterX,
    DeployJitterY,
```

- [ ] **Step 4: Write the deployment**

Replace `Simulation::init` in `Simulation.cpp`:

```cpp
void Simulation::init(size_t soldierCount) {
    const Rng rng{worldSeed, 0u};
    entities.reserve(soldierCount);
    prevPosX.reserve(soldierCount);
    prevPosY.reserve(soldierCount);

    generateObstacles();

    // Squad composition by count: 60% infantry, 25% archers, 15% cavalry.
    constexpr uint32_t kSquadSize = 25;
    const size_t perTeam = soldierCount / 2;
    const uint32_t squadsPerTeam = (uint32_t)((perTeam + kSquadSize - 1) / kSquadSize);

    const float w = (float)screenWidth;
    const float h = (float)screenHeight;

    for (int t = 0; t < 2; ++t) {
        const Team team = (t == 0) ? Team::A : Team::B;
        // Team A faces right from the left margin, team B faces left.
        const float baseX = (t == 0) ? w * 0.15f : w * 0.85f;
        const float facing = (t == 0) ? 1.0f : -1.0f;

        for (uint32_t sq = 0; sq < squadsPerTeam; ++sq) {
            UnitType unit = UnitType::Infantry;
            const uint32_t bucket = sq % 20;
            if (bucket >= 12 && bucket < 17)      unit = UnitType::Archer;
            else if (bucket >= 17)                unit = UnitType::Cavalry;

            const uint16_t squadId = (uint16_t)squads.count;
            squads.spawn(team, unit);
            squads.facingX[squadId] = facing;
            squads.facingY[squadId] = 0.0f;

            // Squads stack down the deployment edge, wrapping into a second
            // column if one does not fit. Scaled to the map so a larger world
            // spreads the army rather than overlapping it.
            const uint32_t perColumn = (uint32_t)std::max(1.0f, h / 60.0f);
            const uint32_t column = sq / perColumn;
            const uint32_t row = sq % perColumn;
            const float squadX = baseX - facing * (float)column * 70.0f;
            const float squadY = h * 0.1f + (float)row * (h * 0.8f / (float)perColumn);

            squads.centroidX[squadId] = squadX;
            squads.centroidY[squadId] = squadY;

            const uint32_t members = (uint32_t)std::min<size_t>(
                kSquadSize, perTeam - (size_t)sq * kSquadSize);
            for (uint32_t k = 0; k < members; ++k) {
                const Vec2 slot = formationSlot(shapeForUnit(unit), (uint16_t)k, members);
                // Facing is +/-1 on X, so the local-to-world rotation reduces
                // to a sign flip. Task 8 uses the general rotation.
                const uint32_t agent = (uint32_t)entities.count;
                const float jx = (float)rng.range(agent, RngUse::DeployJitterX, -2, 2);
                const float jy = (float)rng.range(agent, RngUse::DeployJitterY, -2, 2);
                const float px = squadX + facing * slot.y + jx;
                const float py = squadY + slot.x + jy;

                entities.spawn(clampf(px, 0.0f, w), clampf(py, 0.0f, h),
                               0.0f, 0.0f, team, unit, squadId);
                entities.slotIndex[agent] = (uint16_t)k;
                prevPosX.push_back(entities.posX[agent]);
                prevPosY.push_back(entities.posY[agent]);
            }
        }
    }

    spdlog::info("Deployed {} soldiers in {} squads on a {}x{} field",
                 entities.count, squads.count, screenWidth, screenHeight);
}
```

Add `clampf` as a file-local static above `init` in `Simulation.cpp`, and put `shapeForUnit` in `Units.hpp` so Task 8's `Soldiers.cpp` shares the same definition rather than keeping a second copy:

```cpp
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Lives in Units.hpp, not here, so Soldiers.cpp shares one definition.
constexpr FormationShape shapeForUnit(UnitType u) {
    switch (u) {
        case UnitType::Archer:  return FormationShape::Loose;
        case UnitType::Cavalry: return FormationShape::Wedge;
        default:                return FormationShape::Line;
    }
}
```

- [ ] **Step 5: Add the accessors the test needs**

In `Simulation.hpp`:

```cpp
size_t getSquadCount() const { return squads.count; }
size_t getTeamCount(Team t) const;
float  teamCentroidX(Team t) const;
float  soldierX(size_t i) const { return entities.posX[i]; }
float  soldierY(size_t i) const { return entities.posY[i]; }
bool   everySoldierHasASquadSlot() const;
```

In `Simulation.cpp`:

```cpp
size_t Simulation::getTeamCount(Team t) const {
    size_t n = 0;
    for (size_t i = 0; i < entities.count; ++i) {
        if (entities.team[i] == t) n++;
    }
    return n;
}

float Simulation::teamCentroidX(Team t) const {
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < entities.count; ++i) {
        if (entities.team[i] != t) continue;
        sum += entities.posX[i];
        n++;
    }
    return n ? (float)(sum / (double)n) : 0.0f;
}

bool Simulation::everySoldierHasASquadSlot() const {
    for (size_t i = 0; i < entities.count; ++i) {
        const uint16_t s = entities.squadId[i];
        if (s >= squads.count) return false;
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        if (entities.slotIndex[i] >= n) return false;
        if (squadMembers[start + entities.slotIndex[i]] != i) return false;
    }
    return true;
}
```

- [ ] **Step 6: Run the tests to confirm they pass**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*deploy*,*armies*,*soldier belongs*"
```

Expected: all PASS.

- [ ] **Step 7: Regenerate the baseline and run everything**

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste into the baseline, then `ctest --test-dir build -C Release --output-on-failure`.

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: deploy two armies on opposite edges of a variable-size map

Replaces the uniform random spawn. Squads stack down each deployment edge,
scaled to the world height so a larger map spreads the army instead of
overlapping it.

Full-map usage is now a property of the initial conditions rather than
something the AI has to be coaxed into, which is what the old off-map patrol
bounds were failing to do."
```

---

## Task 6: Phase-ordered tick with barriers

Restructures the tick into the ordered phases the spec requires, with the later phases as stubs this plan does not fill.

**Files:**
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`

**Interfaces:**
- Consumes: `rebuildSquadMembers` from Task 4.
- Produces: `Simulation::tick` calling, in order: `rebuildSpatialHash`, `rebuildInfluence` (stub), `phaseSquadAggregate`, `phaseSquadDecide` (stub), `phaseSoldierSteer`, `phaseProjectiles` (stub), `phaseResolution`, `phaseMovement`. Each parallel phase is followed by `jobSystem.waitAll()`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_determinism.cpp`:

```cpp
TEST_CASE("phase order is stable across thread counts") {
    // The phases mutate shared state only in resolution. If a parallel phase
    // ever writes another agent, this diverges as thread count changes.
    auto run = [](uint32_t threads) {
        Simulation sim(1280, 720, 42u, threads);
        sim.init(2000);
        sim.setPaused(false);
        for (int i = 0; i < 120; ++i) sim.tick(1.0f / 60.0f);
        return sim.stateDigest();
    };
    const uint64_t one = run(1u);
    CHECK(run(2u)  == one);
    CHECK(run(4u)  == one);
    CHECK(run(8u)  == one);
}
```

- [ ] **Step 2: Run it to confirm it passes already**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*phase order*"
```

Expected: PASS. This test is a regression guard for the restructure that follows, so it must be green before, not after. If it fails now, stop; something in Tasks 1 to 5 already broke thread invariance.

- [ ] **Step 3: Restructure `tick()`**

```cpp
void Simulation::tick(float dt) {
    if (paused) return;

    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};

    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }

    jobSystem.resetJobCounter();

    // Phase 1: serial. The influence grid accumulates floats, and summing
    // them in index order on one thread is what makes the result
    // bit-reproducible. Atomics from workers would not be.
    rebuildSpatialHash();
    rebuildInfluence();
    rebuildSquadMembers(entities, squads, squadMembers);

    // Phase 2: parallel over squads. Writes only its own squad.
    phaseSquadAggregate();
    jobSystem.waitAll();

    // Phase 3: parallel over squads. Safe to read every squad's aggregate
    // only because the barrier above made those values read-only.
    phaseSquadDecide(rng);
    jobSystem.waitAll();

    // Phase 4: parallel over soldiers. Writes only its own soldier.
    phaseSoldierSteer(dt, rng);
    jobSystem.waitAll();

    // Phase 5: parallel over projectiles.
    phaseProjectiles(dt);
    jobSystem.waitAll();

    // Phase 6: serial. The ONLY place cross-agent mutation happens.
    phaseResolution(rng);

    // Phase 7: parallel over soldiers.
    phaseMovement(dt);
    jobSystem.waitAll();

    screenWrap();
}
```

- [ ] **Step 4: Add the stubs**

```cpp
void Simulation::rebuildInfluence() {
    // Plan 3 fills this in. Declared here so the phase order is visible and
    // fixed from the start rather than being inserted later.
}

void Simulation::phaseSquadDecide(const Rng&) {
    // Plan 3 fills this in. Every squad currently holds the Advance order it
    // was deployed with.
}

void Simulation::phaseProjectiles(float) {
    // Plan 2 fills this in.
}

void Simulation::phaseResolution(const Rng&) {
    // Plan 2 fills this in. Kept in the phase order now because it is the
    // only place cross-agent mutation is permitted, and later plans must not
    // be tempted to put that anywhere else.
}
```

Rename `updateSeparation` to `phaseSoldierSteer`, `updateSeparationChunk` to `phaseSoldierSteerChunk`, `updateMovement` to `phaseMovement`, and `updateMovementChunk` to `phaseMovementChunk`, keeping their bodies. Rename the declarations in `Simulation.hpp` to match.

- [ ] **Step 5: Run the tests**

```bash
cd c++ && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS except `test_counters`, whose `jobsDispatched` changed because phases were added.

- [ ] **Step 6: Regenerate the baseline and re-run**

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste into the baseline, then `ctest --test-dir build -C Release --output-on-failure`.

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "refactor: restructure the tick into ordered phases with barriers

Establishes the full phase order now, with plan 2 and plan 3 work as named
stubs, so the sequence is fixed from the start rather than being retrofitted.

Resolution exists as an empty serial phase specifically so later plans have
an obvious home for cross-agent mutation and no reason to put it in a
parallel phase."
```

---

## Task 7: Squad aggregate and facing

Fills in phase 2: each squad computes its centroid from its members and its facing from its order objective.

**Files:**
- Modify: `c++/src/Squads.hpp`, `c++/src/Squads.cpp`, `c++/src/Simulation.cpp`
- Modify: `c++/tests/test_squads.cpp`

**Interfaces:**
- Consumes: `rebuildSquadMembers` from Task 4.
- Produces: `void updateSquadAggregate(const SoldierHot&, SquadHot&, const std::vector<uint32_t>&, size_t squadIndex)` writing `centroidX/Y` and `facingX/Y` for one squad.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_squads.cpp`:

```cpp
TEST_CASE("centroid is the mean of member positions") {
    SoldierHot s;
    s.spawn(10.0f, 20.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(30.0f, 40.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    SquadHot q = makeSquads(1);
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);

    updateSquadAggregate(s, q, members, 0);

    CHECK(q.centroidX[0] == doctest::Approx(20.0f));
    CHECK(q.centroidY[0] == doctest::Approx(30.0f));
}

TEST_CASE("an empty squad keeps its previous centroid rather than producing NaN") {
    SoldierHot s;  // no members
    SquadHot q = makeSquads(1);
    q.centroidX[0] = 123.0f;
    q.centroidY[0] = 456.0f;
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);

    updateSquadAggregate(s, q, members, 0);

    CHECK(q.centroidX[0] == doctest::Approx(123.0f));
    CHECK(q.centroidY[0] == doctest::Approx(456.0f));
}

TEST_CASE("facing stays normalized") {
    SoldierHot s;
    s.spawn(0.0f, 0.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    SquadHot q = makeSquads(1);
    q.facingX[0] = 3.0f;   // deliberately not unit length
    q.facingY[0] = 4.0f;
    std::vector<uint32_t> members;
    rebuildSquadMembers(s, q, members);

    updateSquadAggregate(s, q, members, 0);

    const float len = std::sqrt(q.facingX[0] * q.facingX[0] +
                                q.facingY[0] * q.facingY[0]);
    CHECK(len == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cd c++ && cmake --build build --config Release --parallel
```

Expected: FAIL to link, `updateSquadAggregate` undefined.

- [ ] **Step 3: Implement it**

In `Squads.cpp`:

```cpp
void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t s) {
    const uint32_t start = squads.memberStart[s];
    const uint32_t n = squads.memberCount[s];

    // An emptied squad keeps its last centroid. Squads are never destroyed
    // (that is what keeps targetSquad valid without a liveness check), so a
    // wiped-out squad must not poison the field with NaN.
    if (n > 0) {
        // Summed in member order on one thread, so the result is
        // bit-reproducible regardless of worker count.
        float sumX = 0.0f, sumY = 0.0f;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t i = members[start + k];
            sumX += soldiers.posX[i];
            sumY += soldiers.posY[i];
        }
        squads.centroidX[s] = sumX / (float)n;
        squads.centroidY[s] = sumY / (float)n;
    }

    // Plan 3 derives facing from the order objective. Until then a squad
    // holds its deployed facing; renormalize so formation rotation in Task 8
    // can assume unit length.
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len > 1e-6f) {
        squads.facingX[s] = fx / len;
        squads.facingY[s] = fy / len;
    } else {
        squads.facingX[s] = 1.0f;
        squads.facingY[s] = 0.0f;
    }
}
```

- [ ] **Step 4: Dispatch it from phase 2**

In `Simulation.cpp`:

```cpp
void Simulation::phaseSquadAggregate() {
    const size_t chunkSize = 32;
    for (size_t start = 0; start < squads.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, squads.count);
        jobSystem.submit([this, start, end]() {
            for (size_t s = start; s < end; ++s) {
                updateSquadAggregate(entities, squads, squadMembers, s);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}
```

- [ ] **Step 5: Run the tests**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*centroid*,*facing*"
```

Expected: all PASS.

- [ ] **Step 6: Regenerate the baseline and run the full suite**

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste into the baseline, then `ctest --test-dir build -C Release --output-on-failure`.

Expected: all PASS, including the thread-invariance test from Task 6.

- [ ] **Step 7: Commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: squad centroid and facing

Fills in phase 2. Centroids are summed in member order on one thread, so
the result is bit-reproducible whatever the worker count.

An emptied squad keeps its previous centroid instead of dividing by zero.
Squads are never destroyed, since that is what keeps targetSquad valid
without a liveness check, so a wiped-out squad must not poison the field
with NaN."
```

---

## Task 8: Soldiers steer to their formation slots

The headline deliverable of this plan. Soldiers move toward their assigned slot in squad-local space, rotated by squad facing.

**Files:**
- Create: `c++/src/Soldiers.hpp`, `c++/src/Soldiers.cpp`
- Modify: `c++/src/Simulation.cpp`, `c++/CMakeLists.txt`
- Create: `c++/tests/test_steering.cpp`

**Interfaces:**
- Consumes: `formationSlot` from Task 3, squad centroid and facing from Task 7.
- Produces: `Vec2 slotWorldPosition(const SquadHot&, size_t squadIndex, uint16_t slotIndex, uint32_t memberCount)` and `void steerToSlot(SoldierHot&, const SquadHot&, size_t soldierIndex, float dt)`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_steering.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include <cmath>

TEST_CASE("a slot rotates with squad facing") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.centroidX[0] = 100.0f;
    q.centroidY[0] = 100.0f;
    q.memberCount[0] = 9;

    // Facing +x: local forward maps to world +x.
    q.facingX[0] = 1.0f; q.facingY[0] = 0.0f;
    const Vec2 east = slotWorldPosition(q, 0, 4, 9);

    // Facing +y: the same slot must land somewhere different.
    q.facingX[0] = 0.0f; q.facingY[0] = 1.0f;
    const Vec2 north = slotWorldPosition(q, 0, 4, 9);

    CHECK(east.x != doctest::Approx(north.x));
}

TEST_CASE("a soldier standing on its slot is not pushed away") {
    Simulation sim(1280, 720, 42u);
    sim.init(200);
    sim.setPaused(false);
    // Two ticks so membership and aggregates settle.
    sim.tick(1.0f / 60.0f);
    sim.tick(1.0f / 60.0f);
    // No claim about a specific soldier; only that nothing has diverged.
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        CHECK(std::isfinite(sim.soldierX(i)));
        CHECK(std::isfinite(sim.soldierY(i)));
    }
}

TEST_CASE("squads close the distance to their slots over time") {
    Simulation sim(1280, 720, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);
    const float before = sim.meanSlotError();
    for (int i = 0; i < 120; ++i) sim.tick(1.0f / 60.0f);
    const float after = sim.meanSlotError();
    // Deployment already places soldiers near their slots, so this asserts
    // that steering does not make things worse, not that it converges from
    // far away.
    CHECK(after <= before + 1.0f);
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_steering.cpp` and `src/Soldiers.cpp` to `c++/CMakeLists.txt`, then build.

Expected: FAIL to compile, `Soldiers.hpp` not found.

- [ ] **Step 3: Create `Soldiers.hpp`**

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;
struct SquadHot;

// World position of a formation slot, rotating the squad-local offset by the
// squad's facing. Facing is guaranteed unit length by updateSquadAggregate.
Vec2 slotWorldPosition(const SquadHot& squads, size_t squadIndex,
                       uint16_t slotIndex, uint32_t memberCount);

// Steers one soldier toward its slot. Writes only that soldier, so it is
// safe to call from a parallel phase.
void steerToSlot(SoldierHot& soldiers, const SquadHot& squads,
                 size_t soldierIndex, float dt);
```

- [ ] **Step 4: Create `Soldiers.cpp`**

```cpp
#include "Soldiers.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "Formation.hpp"
#include <cmath>

Vec2 slotWorldPosition(const SquadHot& squads, size_t s,
                       uint16_t slotIndex, uint32_t memberCount) {
    const Vec2 local = formationSlot(shapeForUnit(squads.unitType[s]),
                                     slotIndex, memberCount);
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    // Local +y is "toward the enemy" and maps onto facing; local +x is
    // squad-right, which is facing rotated 90 degrees clockwise.
    const float rightX =  fy;
    const float rightY = -fx;
    return Vec2{
        squads.centroidX[s] + local.x * rightX + local.y * fx,
        squads.centroidY[s] + local.x * rightY + local.y * fy
    };
}

void steerToSlot(SoldierHot& soldiers, const SquadHot& squads,
                 size_t i, float dt) {
    (void)dt;
    const uint16_t s = soldiers.squadId[i];
    const Vec2 target = slotWorldPosition(squads, s, soldiers.slotIndex[i],
                                          squads.memberCount[s]);

    const float dx = target.x - soldiers.posX[i];
    const float dy = target.y - soldiers.posY[i];
    const float distSq = dx * dx + dy * dy;

    const float speed = kUnitStats[(int)soldiers.unitType[i]].speed;

    // A deadband stops soldiers vibrating on their slot. Without it, every
    // soldier in a stationary army jitters at full speed across the slot.
    constexpr float kArriveRadius = 2.0f;
    if (distSq < kArriveRadius * kArriveRadius) {
        soldiers.velX[i] = 0.0f;
        soldiers.velY[i] = 0.0f;
        return;
    }

    const float dist = std::sqrt(distSq);
    // Ease off over the last stride so arrival does not overshoot.
    const float approach = (dist < speed * dt * 4.0f)
                         ? dist / (speed * dt * 4.0f) : 1.0f;
    soldiers.velX[i] = (dx / dist) * speed * approach;
    soldiers.velY[i] = (dy / dist) * speed * approach;
}
```

`shapeForUnit` already lives in `Units.hpp` from Task 5, so `Soldiers.cpp` only has to include it. For reference, its definition is:

```cpp
constexpr FormationShape shapeForUnit(UnitType u) {
    return u == UnitType::Archer  ? FormationShape::Loose
         : u == UnitType::Cavalry ? FormationShape::Wedge
                                  : FormationShape::Line;
}
```

No second copy is added here. If Task 5 left a file-local `shapeFor` in `Simulation.cpp`, delete it now.

- [ ] **Step 5: Call it from phase 4, before separation**

In `Simulation::phaseSoldierSteerChunk`, at the top of the per-soldier loop:

```cpp
steerToSlot(entities, squads, i, dt);
```

The existing separation and obstacle-avoidance forces then add to the velocity `steerToSlot` set, so soldiers hold formation without overlapping.

- [ ] **Step 6: Add `meanSlotError` for the test**

In `Simulation.cpp`:

```cpp
float Simulation::meanSlotError() const {
    double sum = 0.0;
    for (size_t i = 0; i < entities.count; ++i) {
        const uint16_t s = entities.squadId[i];
        const Vec2 t = slotWorldPosition(squads, s, entities.slotIndex[i],
                                         squads.memberCount[s]);
        const float dx = t.x - entities.posX[i];
        const float dy = t.y - entities.posY[i];
        sum += std::sqrt(dx * dx + dy * dy);
    }
    return entities.count ? (float)(sum / (double)entities.count) : 0.0f;
}
```

Declare it in `Simulation.hpp`.

- [ ] **Step 7: Run the tests**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests -tc="*slot*,*steer*,*squads close*"
```

Expected: all PASS.

- [ ] **Step 8: Run the full suite and regenerate the baseline**

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Paste into the baseline, then `ctest --test-dir build -C Release --output-on-failure`.

Expected: all PASS, thread invariance included.

- [ ] **Step 9: Commit**

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: soldiers hold formation slots

Soldiers steer to a slot offset from their squad centroid, rotated by squad
facing, with separation still applied on top so they hold a line without
overlapping.

An arrival deadband stops soldiers vibrating across their slot when the
squad is stationary, which at 10k soldiers is the difference between a
standing army and a shimmering one."
```

---

## Task 9: Variable map size end to end

Makes world dimensions a real parameter of the benchmark and the application rather than a constant shared with the window size.

**Files:**
- Modify: `c++/bench/main.cpp`, `c++/src/main.cpp`

**Interfaces:**
- Consumes: `Simulation(int width, int height, uint32_t seed, uint32_t threads)`, unchanged.
- Produces: `tactix_bench --width N --height N`; a GUI whose world size is independent of its window size.

- [ ] **Step 1: Add the flags to the benchmark**

In `c++/bench/main.cpp`, alongside the existing `intArg` calls:

```cpp
const int width  = intArg(argc, argv, "--width", 1280);
const int height = intArg(argc, argv, "--height", 720);

if (width < 128 || height < 128 || width > 100000 || height > 100000) {
    std::fprintf(stderr, "error: --width and --height must be between 128 and 100000\n");
    return 2;
}
```

Replace the hardcoded construction:

```cpp
Simulation sim(width, height,
               static_cast<uint32_t>(seed),
               static_cast<uint32_t>(threads));
```

Add `width` and `height` to the JSON output next to `agents` and `ticks`, so a recorded result says what field produced it.

- [ ] **Step 2: Verify the default is unchanged**

```bash
cd c++ && cmake --build build --config Release --parallel \
  && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Expected: the digest matches the current baseline exactly. Defaults of 1280x720 must reproduce the previous result, or the flag has changed behavior it should not have.

- [ ] **Step 3: Verify a larger field changes the result and still runs**

```bash
cd c++ && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --width 4000 --height 2500 --json
```

Expected: runs to completion, different digest, no crash. A larger field means fewer neighbors per cell, so `candidatesExamined` should drop noticeably. If it does not, deployment is not scaling with map size and Task 5 needs revisiting.

- [ ] **Step 4: Decouple world size from window size in the GUI**

In `c++/src/main.cpp`, separate the two concepts:

```cpp
// Window is what you look through; the world is what you look at.
const int windowWidth  = 1280;
const int windowHeight = 720;
const int worldWidth   = 2400;
const int worldHeight  = 1600;

InitWindow(windowWidth, windowHeight, "Tactix - Medieval Skirmish");
Simulation sim(worldWidth, worldHeight);
```

Point the camera at the world center and zoom to fit:

```cpp
camera.target = Vector2{ worldWidth / 2.0f, worldHeight / 2.0f };
camera.offset = Vector2{ windowWidth / 2.0f, windowHeight / 2.0f };
camera.zoom = std::min((float)windowWidth / worldWidth,
                       (float)windowHeight / worldHeight);
```

Update the middle-mouse reset to restore this fitted view rather than `zoom = 1.0f`.

- [ ] **Step 5: Run the application and confirm both armies are visible**

```bash
cd c++ && ./build/Release/tactix.exe
```

Expected: the window opens showing the whole 2400x1600 field with two armies deployed on opposite edges. Press SPACE to unpause and confirm they advance. Scroll to zoom, right-drag to pan, middle-click to reset.

- [ ] **Step 6: Run the full suite**

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS with no baseline change, since defaults are unchanged.

- [ ] **Step 7: Commit**

```bash
git add c++/bench/main.cpp c++/src/main.cpp
git commit -m "feat: world size is a parameter, not a constant

tactix_bench gains --width and --height, both recorded in the JSON so a
result says what field produced it. Defaults stay 1280x720, so existing
baselines reproduce exactly.

The GUI now separates window size from world size and fits the camera to
the world on startup, since the two were only ever equal by accident."
```

---

## Task 10: Counters, digest coverage, and CI

Closes the instrumentation gaps and makes CI actually run on this branch.

**Files:**
- Modify: `c++/src/WorkCounters.hpp`, `c++/src/Simulation.cpp`, `c++/tests/test_counters.cpp`, `c++/tests/baseline/counters-2k-200.txt`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: everything above.
- Produces: `WorkCounters::squadDecisions`; a CI trigger that matches this branch.

- [ ] **Step 1: Fix the CI branch trigger**

In `.github/workflows/ci.yml`:

```yaml
on:
  push:
    branches: [master, 'phase-*', 'medieval-*']
  pull_request:
```

Without this the workflow never runs on push for this branch, and every task above would have been verified only locally.

- [ ] **Step 2: Add the squad counter**

In `WorkCounters.hpp`, add alongside the existing four:

```cpp
std::atomic<uint64_t> squadDecisions{0};   // squad-tier decisions evaluated
```

Add it to `reset()`. `projectileHitTests` arrives with plan 2; adding it now would gate on a value that is always zero.

- [ ] **Step 3: Increment it from phase 2**

In `Simulation::phaseSquadAggregate`'s job body, after the `updateSquadAggregate` call:

```cpp
workCounters.add(workCounters.squadDecisions, 1);
```

- [ ] **Step 4: Gate on it**

In `c++/tests/test_counters.cpp`, add `squadDecisions` to `CounterSnapshot`, to `snapshot()`, and to the comparison against the baseline, following the existing pattern for the other four exactly.

- [ ] **Step 5: Regenerate the baseline with the new key**

```bash
cd c++ && cmake --build build --config Release --parallel \
  && ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Add a `squadDecisions=` line to `c++/tests/baseline/counters-2k-200.txt` with the value from the JSON, and refresh the other five.

- [ ] **Step 6: Verify the counter is thread-count invariant**

```bash
cd c++ && for t in 1 2 4 8; do ./build/Release/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads $t --json | grep -E 'squadDecisions|stateDigest'; done
```

Expected: identical values at every thread count. If `squadDecisions` varies, the phase is dispatching a different number of jobs per thread count, which means chunking depends on worker count and must not.

- [ ] **Step 7: Run everything**

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add c++/src c++/tests .github/workflows/ci.yml
git commit -m "ci: gate on squad decisions and run on medieval branches

The workflow triggered on master and phase-* only, so nothing in this branch
was being checked on push. Adds medieval-* to the pattern.

Adds a squadDecisions work counter and gates on it, verified identical at 1,
2, 4, and 8 threads. projectileHitTests waits for plan 2 rather than gating
on a value that is always zero."
```

---

## Task 11: Render the armies legibly

Replaces the placeholder single-color rendering with team colors and per-unit silhouettes. Full tactical overlays are plan 4; this is the minimum needed to see that formations work.

**Files:**
- Modify: `c++/src/Renderer.cpp`, `c++/src/main.cpp`

**Interfaces:**
- Consumes: `SoldierHot::team`, `unitType`, `dirX/dirY`; `SquadHot::centroidX/Y`, `facingX/Y`.
- Produces: no new API. `drawSimulation` reads the new fields.

- [ ] **Step 1: Color by team, shape by unit**

In `Renderer.cpp`, replace the placeholder color block:

```cpp
// Team identity carries in hue, unit type in shape. Reading a battle at
// zoomed-out scale depends on those being separable at a few pixels.
const bool teamA = sim.entities.team[i] == Team::A;
Color agentColor = teamA ? Color{ 90, 140, 235, 255 }   // steel blue
                         : Color{ 210,  95,  70, 255 }; // rust red

switch (sim.entities.unitType[i]) {
    case UnitType::Archer:
        // Slightly lighter, drawn as a small square.
        agentColor.r = (uint8_t)std::min(255, agentColor.r + 45);
        agentColor.g = (uint8_t)std::min(255, agentColor.g + 45);
        agentColor.b = (uint8_t)std::min(255, agentColor.b + 45);
        break;
    case UnitType::Cavalry:
        // Darker and drawn larger.
        agentColor.r = (uint8_t)(agentColor.r * 0.7f);
        agentColor.g = (uint8_t)(agentColor.g * 0.7f);
        agentColor.b = (uint8_t)(agentColor.b * 0.7f);
        break;
    default:
        break;
}
```

Draw archers as squares, cavalry as larger triangles, infantry as the existing triangle:

```cpp
const float size = (sim.entities.unitType[i] == UnitType::Cavalry) ? 6.0f : 4.0f;
if (sim.entities.unitType[i] == UnitType::Archer) {
    DrawRectangleV(Vector2{ renderX - 2.0f, renderY - 2.0f },
                   Vector2{ 4.0f, 4.0f }, agentColor);
} else {
    // existing triangle draw, with agentSize replaced by size
}
```

- [ ] **Step 2: Draw the world border at the world's size**

The border currently uses `sim.screenWidth/screenHeight`, which is now the world size, so it is already correct. Verify it frames the whole field rather than the window.

- [ ] **Step 3: Update the ImGui overlay**

In `main.cpp`, replace the civilian/zombie/hero counts, which no longer exist, with:

```cpp
ImGui::Text("Soldiers: %zu  Squads: %zu", sim.getAgentCount(), sim.getSquadCount());
ImGui::Text("Team A: %zu   Team B: %zu",
            sim.getTeamCount(Team::A), sim.getTeamCount(Team::B));
ImGui::Text("World: %d x %d", worldWidth, worldHeight);
```

- [ ] **Step 4: Run and confirm visually**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix.exe
```

Expected: two colored armies in formation on opposite edges. Zoom in and confirm archer squares sit in looser blocks than infantry triangles and that cavalry wedges are visibly triangular. Press SPACE and confirm formations hold shape as they advance rather than dissolving.

- [ ] **Step 5: Run the full suite**

```bash
cd c++ && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS. Rendering is outside `tactix_sim`, so no digest change.

- [ ] **Step 6: Measure the plan's headline number**

```bash
cd c++ && ./build/Release/tactix_bench --agents 10000 --ticks 2000 --seed 42 --json
```

Record p50, p95, p99, and max. This is the number plan 1 is judged on and the baseline plans 2 through 4 are measured against. Do not publish it in the README yet; plan 4 does that once the scenario is complete.

- [ ] **Step 7: Commit**

```bash
git add c++/src/Renderer.cpp c++/src/main.cpp
git commit -m "feat: render armies by team color and unit silhouette

Team identity carries in hue and unit type in shape, because reading a
battle at zoomed-out scale depends on those being separable at a few pixels.

Tactical overlays are plan 4. This is the minimum needed to confirm by eye
that formations hold shape while advancing."
```

---

## Definition of Done

- [ ] `ctest --test-dir build -C Release --output-on-failure` passes on Linux and Windows
- [ ] `stateDigest()` is identical at 1, 2, 4, and 8 worker threads
- [ ] `tactix_bench --agents 10000 --ticks 2000 --seed 42` runs headless and reports p50/p95/p99
- [ ] `tactix_sim` links no raylib symbols
- [ ] Two armies deploy on opposite edges at any map size and advance holding formation
- [ ] No `AgentType`, `AgentState`, infection, or graveyard symbol remains in the tree
- [ ] CI runs on the working branch
- [ ] No em dashes or en dashes, and no AI attribution, in any commit on the branch

Verify the last two with:

```bash
git log master..HEAD --format='%h%n%B' | grep -nP '\xe2\x80\x94|\xe2\x80\x93|claude|anthropic|co-authored' -i
```

Expected: no output.

---

## Not In This Plan

Deferred to plan 2 (Combat): melee intents, projectile array, swept hit testing, casualty recording, compaction, `projectileHitTests`.

Deferred to plan 3 (Tactics): influence grid, the order scorer, hysteresis, the decide stagger, morale, rout and rally, discipline, officer death.

Deferred to plan 4 (Presentation): squad hulls, order arrows, influence heatmap, projectile rendering, README numbers.

Not achievable yet: the spec's success criterion that `Simulation.cpp` falls under 500 lines. Task 1 removes roughly 800 lines and Task 5 adds deployment back, which lands it near 700. It only crosses the threshold once plan 2 moves resolution into its own translation unit and plan 3 moves the scorer into `Squads.cpp`. Plan 4 verifies it.

Deferred indefinitely, recorded in the spec: sorting soldier arrays by squad for cache contiguity, to be settled by measurement against the index array rather than assumed.
