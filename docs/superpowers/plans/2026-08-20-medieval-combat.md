# Medieval Skirmish, Plan 2: Combat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two armies close the distance, fight in melee, and shoot arrows that travel and can miss, with casualties removed and squad membership kept consistent.

**Architecture:** All cross-agent effects are written as intent fields on the actor's own slot during parallel phases, then applied in a single-threaded resolution phase in a fixed order. A third SoA array holds arrows in flight, hit-tested with a swept segment against the existing spatial hash. Deaths compact the soldier arrays by swap-with-back, after every step that reads a stored soldier index has run.

**Tech Stack:** C++20, CMake 3.20+, raylib 5.5 (GUI target only), doctest 2.5.3, spdlog 1.14.1. Existing `JobSystem`, `SpatialHash`, `Rng`, `StateDigest`, `DetMath`.

**Spec:** [`docs/superpowers/specs/2026-08-19-tactix-medieval-skirmish-design.md`](../specs/2026-08-19-tactix-medieval-skirmish-design.md)

## Global Constraints

- **Determinism is the hard gate.** Same seed must produce a bit-identical `stateDigest()` at any worker-thread count AND across platforms. CI runs ubuntu-latest and windows-latest against one committed baseline.
- **No parallel phase may mutate another agent.** Writes go to the actor's own index only. Cross-agent effects are deferred to `phaseResolution`, which is single-threaded. This is now enforced by a gating ThreadSanitizer CI job, not just documented.
- **`tick()` is the single owner of every barrier.** Phase functions submit jobs and return without waiting. Never add a `waitAll()` inside a phase function.
- **`tactix_sim` MUST NOT link raylib.** Only the `tactix` GUI target does.
- **Transcendentals go through `detmath::`, never libm.** `std::sqrt` and `std::ceil` are the only permitted exceptions.
- **`Rng` is stateless**, keyed on `(seed, tick, agentIndex, RngUse)`. Enumerator VALUES are part of the hash input: never reorder or delete one, only append immediately before `Count`.
- **`Rng::range` is integer-only:** `int range(uint32_t, RngUse, int lo, int hi)`. Angles and spreads must be expressed in integer milliradians.
- **Float accumulation order must not depend on threading.** Sum in a fixed index order on one thread.
- **CMake source lists are explicit, never GLOB.** Every new `.cpp` and test file must be hand-added to `c++/CMakeLists.txt`.
- **`Simulation::reset()` must clear every vector that `init()` fills.** Two doctest cases compare a reset simulation's digest against a fresh one and will catch a forgotten field. Never weaken them.
- **NO em dashes or en dashes** anywhere: code comments, docs, commit messages, PR bodies.
- **NEVER add `Co-Authored-By` or any AI attribution** to commit messages. This repository's history is part of the portfolio artifact.

## Two Gaps This Plan Must Close First

The spec assumes machinery that plan 3 builds. Combat cannot be tested without minimal versions of it now.

**Armies never meet.** `updateSquadAggregate` computes `centroidX/Y` as the mean of member positions, so the centroid is an output, not a steering input. Nothing can command a squad to move. Task 1 adds an advance offset so a squad's slot targets sit slightly ahead of its centroid, which pulls the whole formation forward.

**Archers cannot reach their own range.** `kUnitStats[Archer].range` is 280px but `kSeekRadius` is 150px, so an archer cannot perceive its own best target. Spec 6.5 resolves this with a squad-assigned `targetSoldier` derived from `targetSquad`, which the order scorer sets. Task 1 sets `targetSquad` to the nearest enemy squad as a placeholder; plan 3 replaces that choice with the weighted scorer while keeping the same field.

Both placeholders live in `phaseSquadDecide`, which is currently an empty stub. Plan 3 replaces the body, not the plumbing.

## Not In This Plan

Deferred to plan 3 (Tactics): the influence grid, the weighted order scorer and its seven orders, hysteresis, the 15-tick decide stagger, morale, rout and rally, discipline and its five hooks, and officer death consequences. Resolution step 5 from spec 5.5 (morale and discipline update) stays a no-op with a comment.

Deferred to plan 4 (Presentation): squad hulls, order arrows, influence heatmap, and README numbers.

Task 5 records `casualties[]` and `officerDied[]` even though nothing consumes them yet, because spec 5.5 step 4 requires them to be captured before compaction destroys the evidence. Plan 3 consumes them. That is deliberate, not dead code.

## File Structure

| File | Responsibility | Status |
|------|----------------|--------|
| `c++/src/Projectiles.hpp` / `.cpp` | `ProjectileHot`, spawn, integrate, swept hit test | Create |
| `c++/src/Combat.hpp` / `.cpp` | Melee target selection, damage application, casualty recording, compaction | Create |
| `c++/src/Simulation.cpp` | Phase orchestration, `phaseResolution` body, `phaseProjectiles` body | Modify |
| `c++/src/Simulation.hpp` | New members, accessors | Modify |
| `c++/src/Squads.cpp` | `targetSquad` and `targetSoldier` selection | Modify |
| `c++/src/Soldiers.cpp` | Advance offset in slot targeting | Modify |
| `c++/src/Units.hpp` | Combat constants (damage, reach, cooldown, arrow speed) | Modify |
| `c++/src/Rng.hpp` | New `RngUse` enumerators | Modify |
| `c++/src/Renderer.cpp` | Arrows and corpses | Modify |
| `c++/tests/test_projectiles.cpp` | Swept hit test, spread bounds, lead | Create |
| `c++/tests/test_combat.cpp` | Melee guards, overkill, compaction, casualty capture | Create |
| `c++/CMakeLists.txt` | New sources and tests | Modify |

`Simulation.cpp` is currently 710 lines. Moving resolution into `Combat.cpp` is what brings it toward the spec's under-500-line criterion.

---

## Task 1: Armies close the distance

Minimal order machinery so the two armies actually meet. Without this there is nothing to fight over and every later task is untestable.

**Files:**
- Modify: `c++/src/Squads.cpp`, `c++/src/Squads.hpp`, `c++/src/Soldiers.cpp`, `c++/src/Soldiers.hpp`, `c++/src/Simulation.cpp`, `c++/src/Units.hpp`
- Create: `c++/tests/test_advance.cpp`
- Modify: `c++/CMakeLists.txt`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `SquadHot` (has `targetSquad`, `order`, `centroidX/Y`, `facingX/Y`), `slotWorldPosition(const SquadHot&, size_t, uint16_t, uint32_t)`.
- Produces: `enum class SquadOrder : uint8_t { Hold = 0, Advance = 1 };` in `Units.hpp` (plan 3 adds the other five). `void selectTargetSquad(SquadHot& squads, size_t squadIndex)` in `Squads.hpp`. `slotWorldPosition` gains an advance offset internally, signature unchanged.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_advance.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Simulation.hpp"
#include <cmath>

TEST_CASE("each squad targets an enemy squad, never a friendly one") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    REQUIRE(sim.getSquadCount() > 1);
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const uint16_t t = sim.squadTargetSquad(s);
        REQUIRE(t < sim.getSquadCount());
        CHECK(sim.squadTeam(t) != sim.squadTeam(s));
    }
}

TEST_CASE("the armies close the distance between them") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    const float startGap = std::fabs(sim.teamCentroidX(Team::B) - sim.teamCentroidX(Team::A));
    for (int i = 0; i < 600; ++i) sim.tick(1.0f / 60.0f);
    const float endGap = std::fabs(sim.teamCentroidX(Team::B) - sim.teamCentroidX(Team::A));

    // Ten seconds of marching must visibly close the gap. Without an advance
    // offset the squads hold formation forever and this stays flat, which is
    // exactly the state this task exists to fix.
    CHECK(endGap < startGap - 100.0f);
}

TEST_CASE("advancing does not tear the formation apart") {
    // The advance offset pulls soldiers forward off their slots. If it is too
    // large they never catch up and the formation stretches without bound.
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 300; ++i) sim.tick(1.0f / 60.0f);
    CHECK(sim.meanSlotError() <= 25.0f);
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `tests/test_advance.cpp` to the `tactix_tests` source list in `c++/CMakeLists.txt`, then:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel
```

Expected: FAIL to compile, `squadTargetSquad` undeclared.

- [ ] **Step 3: Add the order enum and advance constant to `Units.hpp`**

```cpp
// Plan 3 adds FlankLeft, FlankRight, Charge, Withdraw, and Rout. Values are
// part of the state digest, so append new ones rather than renumbering.
enum class SquadOrder : uint8_t { Hold = 0, Advance = 1 };

// How far ahead of its centroid a squad aims its formation slots while
// advancing. Soldiers chase a target slightly in front of where they stand,
// which drags the centroid forward and marches the formation. Kept well under
// kSlotSpacing so the formation does not stretch faster than soldiers close it.
constexpr float kAdvanceLead = 6.0f;
```

- [ ] **Step 4: Add target selection to `Squads.cpp`**

Declare in `Squads.hpp`:

```cpp
// Picks the nearest enemy squad by centroid distance. Parallel-safe: writes
// only the squad it is given, reads other squads' centroids, which phase 2
// already finished writing.
//
// Plan 3 replaces this with the weighted scorer. The FIELD it writes stays the
// same, so only the choice changes, not the plumbing.
void selectTargetSquad(SquadHot& squads, size_t squadIndex);
```

Implement in `Squads.cpp`:

```cpp
void selectTargetSquad(SquadHot& squads, size_t s) {
    if (squads.memberCount[s] == 0) return;

    float bestDistSq = 1e30f;
    uint16_t best = squads.targetSquad[s];
    bool found = false;

    // Walked in ascending index order so ties resolve identically on every
    // thread and platform.
    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;
        if (d < bestDistSq) {
            bestDistSq = d;
            best = (uint16_t)e;
            found = true;
        }
    }

    if (found) {
        squads.targetSquad[s] = best;
        squads.order[s] = (uint8_t)SquadOrder::Advance;
    } else {
        // Every enemy squad is wiped out. Hold rather than advancing on a
        // stale target.
        squads.order[s] = (uint8_t)SquadOrder::Hold;
    }
}
```

- [ ] **Step 5: Dispatch it from `phaseSquadDecide`**

Replace the stub body in `Simulation.cpp`:

```cpp
void Simulation::phaseSquadDecide(const Rng&) {
    // Plan 3 replaces the body of selectTargetSquad with a weighted scorer over
    // seven orders, plus hysteresis and a decide stagger. The dispatch shape
    // here does not change.
    const size_t chunkSize = 32;
    for (size_t start = 0; start < squads.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, squads.count);
        jobSystem.submit([this, start, end]() {
            for (size_t s = start; s < end; ++s) {
                selectTargetSquad(squads, s);
                workCounters.add(workCounters.squadDecisions, 1);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}
```

Remove the `squadDecisions` increment from `phaseSquadAggregate`, so the counter names what it counts. Note this moves the counter's meaning; the baseline changes accordingly.

- [ ] **Step 6: Derive facing from the order, and add the advance lead**

In `Squads.cpp`, inside `updateSquadAggregate`, replace the "retain previous facing" logic with spec 6.6's rule for the two orders that exist:

```cpp
    // Spec 6.6: facing comes from the order's objective, not from averaging
    // soldier directions (noisy for a loose formation) and not from centroid
    // velocity (undefined when stationary).
    if (squads.order[s] == (uint8_t)SquadOrder::Advance) {
        const uint16_t t = squads.targetSquad[s];
        if (t < squads.count && squads.memberCount[t] > 0) {
            const float dx = squads.centroidX[t] - squads.centroidX[s];
            const float dy = squads.centroidY[t] - squads.centroidY[s];
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-6f) {
                squads.facingX[s] = dx / len;
                squads.facingY[s] = dy / len;
            }
        }
    }
    // Hold retains the previous facing, which the normalization below keeps
    // unit length.
```

Keep the existing normalization that follows it.

In `Soldiers.cpp`, inside `slotWorldPosition`, apply the lead:

```cpp
    // While advancing, aim the whole formation slightly ahead of where it
    // stands. Soldiers chase that, the centroid follows them, and the squad
    // marches. Holding squads get no lead, so the centroid stays a fixed point
    // exactly as formationMeanOffset arranged.
    const float lead = (squads.order[s] == (uint8_t)SquadOrder::Advance)
                     ? kAdvanceLead : 0.0f;

    return Vec2{
        squads.centroidX[s] + local.x * rightX + (local.y + lead) * fx,
        squads.centroidY[s] + local.x * rightY + (local.y + lead) * fy
    };
```

- [ ] **Step 7: Add the accessor the test needs**

In `Simulation.hpp`:

```cpp
uint16_t squadTargetSquad(size_t s) const { return squads.targetSquad[s]; }
```

- [ ] **Step 8: Run the tests**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests.exe -tc="*targets an enemy*,*close the distance*,*tear the formation*"
```

Expected: all PASS. If "close the distance" fails, `kAdvanceLead` is too small or facing is not tracking the target. If "tear the formation" fails, the lead is too large relative to unit speed.

- [ ] **Step 9: Verify determinism and regenerate the baseline**

```bash
cd c++ && for t in 1 2 4 8; do ./build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads $t --json | grep -E 'stateDigest|squadDecisions'; done
```

Expected: all four identical. Then regenerate `c++/tests/baseline/counters-2k-200.txt` from a real run and confirm the full suite passes.

- [ ] **Step 10: Commit**

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: squads advance on the nearest enemy squad

Adds the minimum order machinery needed for the armies to meet: a target
squad chosen by centroid distance, facing derived from that target per spec
6.6, and a small forward lead on formation slots so a squad drags itself
along instead of holding position forever.

The lead is what makes a mean-of-members centroid steerable. Soldiers chase a
point slightly ahead of where they stand, the centroid follows them, and the
formation marches without the centroid ever becoming an input.

Plan 3 replaces the nearest-enemy choice with the weighted scorer. It writes
the same field, so only the decision changes."
```

---

## Task 2: The projectile array

The data structure and its wiring, with no behaviour yet. Isolated so a later task's bug cannot be confused with a layout mistake.

**Files:**
- Create: `c++/src/Projectiles.hpp`, `c++/src/Projectiles.cpp`
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/Units.hpp`, `c++/CMakeLists.txt`
- Modify: `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct ProjectileHot` with `posX, posY, velX, velY` (float), `team` (Team), `damage` (uint8_t), `lifetime` (float), `intentHitTarget` (uint32_t, `UINT32_MAX` for none), `count` (size_t), and `void spawn(float px, float py, float vx, float vy, Team t, uint8_t dmg, float life)`. `Simulation` gains a `ProjectileHot projectiles;` member and `size_t getProjectileCount() const`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_projectiles.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to confirm it fails**

Add `src/Projectiles.cpp` to the `tactix_sim` source list and `tests/test_projectiles.cpp` to `tactix_tests` in `c++/CMakeLists.txt`, then build.

Expected: FAIL to compile, `Projectiles.hpp` not found.

- [ ] **Step 3: Create `Projectiles.hpp`**

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

// Arrows in flight. A third SoA array alongside SoldierHot and SquadHot, with
// a very different lifetime pattern: entries are created and destroyed
// constantly rather than persisting for the run.
struct ProjectileHot {
    std::vector<float>    posX;
    std::vector<float>    posY;
    std::vector<float>    velX;
    std::vector<float>    velY;
    std::vector<Team>     team;
    std::vector<uint8_t>  damage;
    std::vector<float>    lifetime;

    // Written by phase 5, consumed by resolution, mirroring the soldier intent
    // pattern. UINT32_MAX means this arrow hit nothing this tick.
    std::vector<uint32_t> intentHitTarget;

    size_t count = 0;

    void spawn(float px, float py, float vx, float vy, Team t, uint8_t dmg, float life) {
        posX.push_back(px);
        posY.push_back(py);
        velX.push_back(vx);
        velY.push_back(vy);
        team.push_back(t);
        damage.push_back(dmg);
        lifetime.push_back(life);
        intentHitTarget.push_back(UINT32_MAX);
        count++;
    }

    void clear() {
        posX.clear();
        posY.clear();
        velX.clear();
        velY.clear();
        team.clear();
        damage.clear();
        lifetime.clear();
        intentHitTarget.clear();
        count = 0;
    }
};
```

- [ ] **Step 4: Create `Projectiles.cpp`**

```cpp
#include "Projectiles.hpp"

// Behaviour arrives in tasks 8 and 9. This translation unit exists now so the
// CMake wiring and the tactix_sim raylib-free guarantee are settled before any
// logic depends on them.
```

- [ ] **Step 5: Add combat constants to `Units.hpp`**

```cpp
// Arrow flight. Speed is deliberately modest: a faster arrow crosses more
// ground per tick, and the swept hit test in task 8 is what keeps that honest.
constexpr float   kArrowSpeed      = 200.0f;  // px/s
constexpr float   kArrowLifetime   = 3.0f;    // seconds before it falls short
constexpr uint8_t kArrowDamage     = 1;
constexpr float   kSoldierRadius   = 4.0f;    // for hit tests
```

- [ ] **Step 6: Wire the member into `Simulation`**

In `Simulation.hpp` add `ProjectileHot projectiles;` beside `squads`, and:

```cpp
size_t getProjectileCount() const { return projectiles.count; }
```

In `Simulation::reset()`, add `projectiles.clear();` alongside the other clears. The two reset-versus-fresh digest tests will catch it if you forget.

- [ ] **Step 7: Extend the digest**

Spec 5.7 requires projectiles in the digest. Add after the per-squad loop in `stateDigest()`:

```cpp
    // Projectiles are included from the moment the array exists, so the
    // thread-invariance gate covers them before anything starts writing them.
    d.mix(static_cast<uint32_t>(projectiles.count));
    for (size_t i = 0; i < projectiles.count; ++i) {
        d.mix(projectiles.posX[i]);
        d.mix(projectiles.posY[i]);
        d.mix(projectiles.velX[i]);
        d.mix(projectiles.velY[i]);
        d.mix(static_cast<uint32_t>(projectiles.team[i]));
        d.mix(projectiles.lifetime[i]);
    }
```

- [ ] **Step 8: Build, test, regenerate baseline, commit**

```bash
cd c++ && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

The digest gains a projectile count of zero, which changes it. Regenerate the baseline from a real run, confirm the suite passes, then:

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: add the projectile array

Structure and wiring only, no behaviour. Included in the state digest and in
reset()'s clear list from the outset, so the thread-invariance gate and the
reset-versus-fresh tests cover projectiles before any code starts writing
them."
```

---

## Task 3: Melee target selection

Soldiers pick an enemy within reach and record it as an intent on their own slot. No damage yet.

**Files:**
- Create: `c++/src/Combat.hpp`, `c++/src/Combat.cpp`, `c++/tests/test_combat.cpp`
- Modify: `c++/src/Simulation.cpp`, `c++/src/Units.hpp`, `c++/CMakeLists.txt`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `SoldierHot`, `SpatialHash::queryNeighbors(float, float, float, std::vector<uint32_t>&)`.
- Produces: `void selectMeleeTarget(SoldierHot&, const SpatialHash&, size_t soldierIndex, std::vector<uint32_t>& scratch)` in `Combat.hpp`. Constants `kMeleeReach`, `kMeleeDamage`, `kMeleeCooldown` in `Units.hpp`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_combat.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "Combat.hpp"
#include <cmath>

TEST_CASE("a soldier never targets its own team") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    // Long enough for the armies to make contact.
    for (int i = 0; i < 900; ++i) sim.tick(1.0f / 60.0f);

    bool anyTarget = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const uint32_t t = sim.soldierIntentTarget(i);
        if (t == UINT32_MAX) continue;
        anyTarget = true;
        REQUIRE(t < sim.getAgentCount());
        CHECK(sim.soldierTeam(t) != sim.soldierTeam(i));
    }
    // If nothing ever engaged, the test proved nothing.
    CHECK(anyTarget);
}

TEST_CASE("a soldier only targets what is within melee reach") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 900; ++i) sim.tick(1.0f / 60.0f);

    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        const uint32_t t = sim.soldierIntentTarget(i);
        if (t == UINT32_MAX) continue;
        const float dx = sim.soldierX(t) - sim.soldierX(i);
        const float dy = sim.soldierY(t) - sim.soldierY(i);
        CHECK(std::sqrt(dx * dx + dy * dy) <= kMeleeReach + 0.01f);
    }
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `src/Combat.cpp` to `tactix_sim` and `tests/test_combat.cpp` to `tactix_tests` in `c++/CMakeLists.txt`, then build.

Expected: FAIL to compile, `Combat.hpp` not found.

- [ ] **Step 3: Add melee constants to `Units.hpp`**

```cpp
// Melee. Reach is deliberately close to kSlotSpacing so that two formations
// have to actually touch before anyone swings.
constexpr float   kMeleeReach    = 14.0f;  // px
constexpr uint8_t kMeleeDamage   = 1;
constexpr float   kMeleeCooldown = 0.8f;   // seconds between swings
```

- [ ] **Step 4: Create `Combat.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <vector>

struct SoldierHot;
class SpatialHash;

// Chooses an enemy within kMeleeReach and records it in the soldier's own
// intentTarget. Parallel-safe: writes only soldierIndex's own slot, and reads
// only positions and teams, which no parallel phase writes.
//
// Ties are broken by lowest soldier index, so the choice is identical at any
// thread count and on any platform.
void selectMeleeTarget(SoldierHot& soldiers, const SpatialHash& hash,
                       size_t soldierIndex, std::vector<uint32_t>& scratch);
```

- [ ] **Step 5: Create `Combat.cpp`**

```cpp
#include "Combat.hpp"
#include "Simulation.hpp"
#include "SpatialHash.hpp"
#include "Units.hpp"
#include <cmath>

void selectMeleeTarget(SoldierHot& soldiers, const SpatialHash& hash,
                       size_t i, std::vector<uint32_t>& scratch) {
    soldiers.intentTarget[i] = UINT32_MAX;

    if (soldiers.state[i] == SoldierState::Dead) return;
    if (soldiers.attackCooldown[i] > 0.0f) return;

    const float px = soldiers.posX[i];
    const float py = soldiers.posY[i];
    hash.queryNeighbors(px, py, kMeleeReach, scratch);

    const float reachSq = kMeleeReach * kMeleeReach;
    float bestSq = reachSq;
    uint32_t best = UINT32_MAX;

    for (uint32_t n : scratch) {
        if ((size_t)n == i) continue;
        if (soldiers.team[n] == soldiers.team[i]) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;

        const float dx = soldiers.posX[n] - px;
        const float dy = soldiers.posY[n] - py;
        const float dSq = dx * dx + dy * dy;

        // Strictly-less keeps the FIRST of any equidistant pair, and
        // queryNeighbors walks cells in a fixed order over insertion-ordered
        // vectors, so the winner is the same on every thread and platform.
        if (dSq < bestSq) {
            bestSq = dSq;
            best = n;
        }
    }

    soldiers.intentTarget[i] = best;
}
```

- [ ] **Step 6: Call it from the steering phase**

In `Simulation::phaseSoldierSteerChunk`, after `steerToSlot` and the separation work, add:

```cpp
        selectMeleeTarget(soldiers, spatialHash, i, localNeighbors);
```

Reuse the chunk's existing thread-local neighbour buffer rather than allocating a new one.

- [ ] **Step 7: Add accessors and tick the cooldown**

In `Simulation.hpp`:

```cpp
uint32_t soldierIntentTarget(size_t i) const { return soldiers.intentTarget[i]; }
Team     soldierTeam(size_t i) const { return soldiers.team[i]; }
uint8_t  soldierHealth(size_t i) const { return soldiers.health[i]; }
```

In `phaseMovementChunk`, decay the cooldown, since that phase already writes only its own soldier:

```cpp
        if (soldiers.attackCooldown[i] > 0.0f) {
            soldiers.attackCooldown[i] -= dt;
        }
```

- [ ] **Step 8: Build, test, verify determinism, regenerate baseline, commit**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix_tests.exe -tc="*never targets its own team*,*within melee reach*"
```

Then the four-thread digest check and a baseline regeneration, and:

```bash
git add c++/src c++/tests c++/CMakeLists.txt c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: soldiers pick a melee target within reach

Target selection only, no damage yet. The choice is written to the soldier's
own intentTarget so the parallel phase never touches another agent, and ties
break on lowest index so the result does not depend on thread count."
```

---

## Task 4: Melee resolution

Applies the intents recorded in task 3, single-threaded, in the fixed order spec 5.5 requires.

**Files:**
- Modify: `c++/src/Combat.hpp`, `c++/src/Combat.cpp`, `c++/src/Simulation.cpp`, `c++/tests/test_combat.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `selectMeleeTarget` from task 3.
- Produces: `void applyMeleeIntents(SoldierHot&)` in `Combat.hpp`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_combat.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `applyMeleeIntents` undeclared.

- [ ] **Step 3: Declare and implement it**

In `Combat.hpp`:

```cpp
// Resolution step 1 (spec 5.5). Applies every soldier's melee intent in
// ascending soldier index order. Single-threaded: this is the only place a
// soldier may write another soldier's health.
void applyMeleeIntents(SoldierHot& soldiers);
```

In `Combat.cpp`:

```cpp
void applyMeleeIntents(SoldierHot& soldiers) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        const uint32_t t = soldiers.intentTarget[i];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;

        // Both ends must still be alive. The attacker may have been killed
        // earlier in this same loop by a lower-indexed soldier, and the target
        // may already have been finished off. Dropping the intent in either
        // case is what makes overkill wasted rather than carried over, and it
        // is also what stops health underflowing past zero.
        if (soldiers.health[i] == 0) continue;
        if (soldiers.health[t] == 0) continue;

        soldiers.health[t] = (soldiers.health[t] > kMeleeDamage)
                           ? (uint8_t)(soldiers.health[t] - kMeleeDamage)
                           : (uint8_t)0;
        soldiers.attackCooldown[i] = kMeleeCooldown;
        soldiers.state[i] = SoldierState::Engaged;
    }

    // Intents are single-use. Clearing here means a stale index can never be
    // read on a later tick, which matters because task 5's compaction
    // renumbers soldiers.
    for (size_t i = 0; i < soldiers.count; ++i) {
        soldiers.intentTarget[i] = UINT32_MAX;
    }
}
```

- [ ] **Step 4: Call it from resolution**

Replace the `phaseResolution` stub body in `Simulation.cpp`:

```cpp
void Simulation::phaseResolution(const Rng&) {
    // Spec 5.5. The order is load-bearing and each step notes what it needs.
    // Single-threaded on purpose: this is the ONLY place cross-agent mutation
    // is permitted anywhere in the tick.
    applyMeleeIntents(soldiers);   // step 1
    // step 2 (projectile hits) arrives in task 9
    // step 3 (arrow spawn) arrives in task 7
    // step 4 (casualties) and 6 (compaction) arrive in task 5
    // step 5 (morale and discipline) is plan 3
}
```

- [ ] **Step 5: Run the tests, verify determinism, regenerate baseline, commit**

```bash
cd c++ && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

Then the four-thread digest check, baseline regeneration, and:

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: resolve melee intents in soldier index order

Resolution step 1. Both attacker and target must still have health, which
makes overkill wasted rather than carried over and stops health underflowing
a uint8_t past zero. Intents are cleared after application so a stale index
can never survive into a later tick.

Applied single-threaded because this is the only place in the tick where one
agent may write another."
```

---

## Task 5: Casualties and compaction

Deaths are recorded before anything is removed, then removed, then membership is rebuilt. This is where the spec's ordering constraint bites hardest.

**Files:**
- Modify: `c++/src/Combat.hpp`, `c++/src/Combat.cpp`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/tests/test_combat.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `applyMeleeIntents` from task 4, `rebuildSquadMembers` from plan 1.
- Produces: `void recordCasualties(SoldierHot&, std::vector<uint32_t>& casualties, std::vector<uint8_t>& officerDied)` and `void compactDead(SoldierHot&, std::vector<float>& prevPosX, std::vector<float>& prevPosY)` in `Combat.hpp`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_combat.cpp`:

```cpp
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(112.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
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
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.spawn(112.0f, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    s.slotIndex[0] = 0;
    s.slotIndex[1] = 1;
    s.health[1] = 0;

    std::vector<uint32_t> casualties(1, 0u);
    std::vector<uint8_t> officerDied(1, 0u);
    recordCasualties(s, casualties, officerDied);

    CHECK(officerDied[0] == 0);
    CHECK(casualties[0] == 1);
}

TEST_CASE("compaction removes the dead and keeps every array the same length") {
    SoldierHot s;
    for (int k = 0; k < 5; ++k) {
        s.spawn(100.0f + k, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    }
    std::vector<float> prevX(5, 0.0f), prevY(5, 0.0f);
    s.state[1] = SoldierState::Dead;
    s.state[3] = SoldierState::Dead;

    compactDead(s, prevX, prevY);

    CHECK(s.count == 3);
    CHECK(s.posX.size() == 3);
    CHECK(s.health.size() == 3);
    CHECK(s.intentTarget.size() == 3);
    CHECK(prevX.size() == 3);
    for (size_t i = 0; i < s.count; ++i) {
        CHECK(s.state[i] != SoldierState::Dead);
    }
}

TEST_CASE("compacting an army with no dead changes nothing") {
    SoldierHot s;
    for (int k = 0; k < 4; ++k) {
        s.spawn(100.0f + k, 100.0f, 0, 0, Team::A, UnitType::Infantry, 0);
    }
    std::vector<float> prevX(4, 0.0f), prevY(4, 0.0f);

    compactDead(s, prevX, prevY);

    CHECK(s.count == 4);
    CHECK(s.posX[3] == doctest::Approx(103.0f));
}

TEST_CASE("soldiers actually die in a running battle") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    const size_t before = sim.getAgentCount();
    for (int i = 0; i < 1800; ++i) sim.tick(1.0f / 60.0f);
    CHECK(sim.getAgentCount() < before);
}
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `recordCasualties` and `compactDead` undeclared.

- [ ] **Step 3: Implement both in `Combat.cpp`**

Declare in `Combat.hpp`:

```cpp
// Resolution step 4 (spec 5.5). Runs BEFORE compaction, because the officer is
// identified by slotIndex 0 and compaction reassigns slots.
void recordCasualties(SoldierHot& soldiers,
                      std::vector<uint32_t>& casualties,
                      std::vector<uint8_t>& officerDied);

// Resolution step 6 (spec 5.5). Swap-with-back removal of Dead soldiers. This
// INVALIDATES every soldier index, so it must run after every step that reads
// one.
void compactDead(SoldierHot& soldiers,
                 std::vector<float>& prevPosX,
                 std::vector<float>& prevPosY);
```

Implement:

```cpp
void recordCasualties(SoldierHot& soldiers,
                      std::vector<uint32_t>& casualties,
                      std::vector<uint8_t>& officerDied) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.health[i] != 0) continue;
        if (soldiers.state[i] == SoldierState::Dead) continue;  // already counted

        soldiers.state[i] = SoldierState::Dead;

        const uint16_t sq = soldiers.squadId[i];
        if (sq < casualties.size()) {
            casualties[sq]++;
            // The officer is whoever holds slot 0. Capture it now: compaction
            // removes this corpse and the next man inherits the slot, after
            // which there is no way to tell an officer died at all.
            if (soldiers.slotIndex[i] == 0) {
                officerDied[sq] = 1;
            }
        }
    }
}

void compactDead(SoldierHot& soldiers,
                 std::vector<float>& prevPosX,
                 std::vector<float>& prevPosY) {
    size_t i = 0;
    while (i < soldiers.count) {
        if (soldiers.state[i] != SoldierState::Dead) {
            ++i;
            continue;
        }

        const size_t last = soldiers.count - 1;
        if (i != last) {
            // Swap-with-back. Every parallel array must move together or the
            // structure of arrays desyncs.
            soldiers.posX[i]            = soldiers.posX[last];
            soldiers.posY[i]            = soldiers.posY[last];
            soldiers.velX[i]            = soldiers.velX[last];
            soldiers.velY[i]            = soldiers.velY[last];
            soldiers.dirX[i]            = soldiers.dirX[last];
            soldiers.dirY[i]            = soldiers.dirY[last];
            soldiers.team[i]            = soldiers.team[last];
            soldiers.unitType[i]        = soldiers.unitType[last];
            soldiers.state[i]           = soldiers.state[last];
            soldiers.squadId[i]         = soldiers.squadId[last];
            soldiers.slotIndex[i]       = soldiers.slotIndex[last];
            soldiers.health[i]          = soldiers.health[last];
            soldiers.attackCooldown[i]  = soldiers.attackCooldown[last];
            soldiers.intentTarget[i]    = soldiers.intentTarget[last];
            soldiers.intentFire[i]      = soldiers.intentFire[last];
            prevPosX[i]                 = prevPosX[last];
            prevPosY[i]                 = prevPosY[last];
            // Do NOT advance i: the soldier just swapped in has not been
            // examined yet and may itself be dead.
        } else {
            ++i;
        }

        soldiers.posX.pop_back();
        soldiers.posY.pop_back();
        soldiers.velX.pop_back();
        soldiers.velY.pop_back();
        soldiers.dirX.pop_back();
        soldiers.dirY.pop_back();
        soldiers.team.pop_back();
        soldiers.unitType.pop_back();
        soldiers.state.pop_back();
        soldiers.squadId.pop_back();
        soldiers.slotIndex.pop_back();
        soldiers.health.pop_back();
        soldiers.attackCooldown.pop_back();
        soldiers.intentTarget.pop_back();
        soldiers.intentFire.pop_back();
        prevPosX.pop_back();
        prevPosY.pop_back();
        soldiers.count--;
    }
}
```

- [ ] **Step 4: Add the scratch buffers and wire resolution**

In `Simulation.hpp`, beside `squadMembers`:

```cpp
// Per-squad scratch, cleared at the start of every resolution phase. Plan 3
// consumes both to drive morale and the officer-death discipline penalty.
std::vector<uint32_t> casualties;
std::vector<uint8_t>  officerDied;
```

Add both to `reset()`'s clear list.

In `Simulation.cpp`:

```cpp
void Simulation::phaseResolution(const Rng&) {
    casualties.assign(squads.count, 0u);
    officerDied.assign(squads.count, 0u);

    applyMeleeIntents(soldiers);                            // step 1
    // step 2 (projectile hits) arrives in task 9
    // step 3 (arrow spawn) arrives in task 7
    recordCasualties(soldiers, casualties, officerDied);    // step 4
    // step 5 (morale and discipline) is plan 3; casualties and officerDied are
    // recorded now precisely so it has something to read when it arrives.
    compactDead(soldiers, prevPosX, prevPosY);              // step 6
    rebuildSquadMembers(soldiers, squads, squadMembers);    // step 7
}
```

- [ ] **Step 5: Remove the now-duplicated rebuild from phase 1**

`rebuildSquadMembers` currently runs near the top of `tick()`. Delete that call: membership is now rebuilt at the end of resolution, which is where spec 5.5 puts it and the only place it can be correct once compaction exists.

`init()` already calls `rebuildSquadMembers` once, so the first tick starts with valid membership.

- [ ] **Step 6: Run the tests**

```bash
cd c++ && cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure
```

Expected: all PASS. If "soldiers actually die in a running battle" fails, the armies are not making contact; check task 1 before suspecting this task.

- [ ] **Step 7: Verify determinism and regenerate the baseline**

```bash
cd c++ && for t in 1 2 4 8; do ./build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads $t --json | grep stateDigest; done
```

All four must match. This is the most important check in this task: compaction renumbers soldiers, and any residual dependence on index ordering shows up here.

- [ ] **Step 8: Commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: record casualties, then compact the dead

Resolution steps 4, 6, and 7. Casualties and officer death are recorded
before anything is removed, because the officer is identified by slotIndex 0
and compaction reassigns slots. By the time a corpse is gone there is no way
to tell an officer died at all.

Compaction is swap-with-back and invalidates every soldier index, so it runs
after every step that reads one. Membership rebuild moves from the top of the
tick to the end of resolution, which is the only place it can be correct once
soldiers are being removed.

casualties and officerDied are populated but unread until plan 3 uses them for
morale and the officer-death discipline penalty."
```

---

## Task 6: Archers acquire a target

The squad assigns a target soldier, because an archer outranges its own perception. This is the concrete payoff of the squad tier.

**Files:**
- Modify: `c++/src/Squads.cpp`, `c++/src/Squads.hpp`, `c++/src/Simulation.cpp`, `c++/tests/test_combat.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `targetSquad` from task 1.
- Produces: `void selectTargetSoldier(const SoldierHot&, SquadHot&, const std::vector<uint32_t>& members, size_t squadIndex)` in `Squads.hpp`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_combat.cpp`:

```cpp
TEST_CASE("an archer squad acquires a target beyond its soldiers' own sight") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 600; ++i) sim.tick(1.0f / 60.0f);

    bool sawLongRangeAcquisition = false;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const uint32_t t = sim.squadTargetSoldier(s);
        if (t == UINT32_MAX) continue;
        REQUIRE(t < sim.getAgentCount());

        const float dx = sim.soldierX(t) - sim.squadCentroidX(s);
        const float dy = sim.soldierY(t) - sim.squadCentroidY(s);
        const float dist = std::sqrt(dx * dx + dy * dy);

        // The whole justification for the squad tier: a target further away
        // than any individual soldier could perceive.
        if (dist > kSeekRadius) sawLongRangeAcquisition = true;
    }
    CHECK(sawLongRangeAcquisition);
}

TEST_CASE("a squad never acquires a target on its own team") {
    Simulation sim(2400, 1600, 42u);
    sim.init(500);
    sim.setPaused(false);
    for (int i = 0; i < 600; ++i) sim.tick(1.0f / 60.0f);

    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const uint32_t t = sim.squadTargetSoldier(s);
        if (t == UINT32_MAX) continue;
        CHECK(sim.soldierTeam(t) != sim.squadTeam(s));
    }
}
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `squadTargetSoldier` undeclared.

- [ ] **Step 3: Implement selection**

Declare in `Squads.hpp`:

```cpp
// Spec 6.5. Picks the member of targetSquad with the LOWEST slotIndex that is
// within weapon range of our centroid, or UINT32_MAX if none is.
//
// Recomputed every tick and never cached across ticks: compaction renumbers
// soldiers, so a stored soldier index is stale the moment anyone dies.
//
// Lowest slotIndex is what makes officers preferentially targeted without a
// special case, since the officer is whoever holds slot 0.
void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t squadIndex);
```

Implement in `Squads.cpp`:

```cpp
void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t s) {
    squads.targetSoldier[s] = UINT32_MAX;

    const float range = kUnitStats[(int)squads.unitType[s]].range;
    if (range <= 0.0f) return;  // melee units acquire their own targets

    const uint16_t t = squads.targetSquad[s];
    if (t >= squads.count || squads.memberCount[t] == 0) return;

    const float cx = squads.centroidX[s];
    const float cy = squads.centroidY[s];
    const float rangeSq = range * range;

    // members is ordered by slotIndex within each squad, so the first member in
    // range is the lowest-slotIndex one. No sort or comparison needed.
    const uint32_t start = squads.memberStart[t];
    for (uint32_t k = 0; k < squads.memberCount[t]; ++k) {
        const uint32_t idx = members[start + k];
        const float dx = soldiers.posX[idx] - cx;
        const float dy = soldiers.posY[idx] - cy;
        if (dx * dx + dy * dy <= rangeSq) {
            squads.targetSoldier[s] = idx;
            return;
        }
    }
}
```

- [ ] **Step 4: Call it from phase 2**

In `Simulation::phaseSquadAggregate`'s job body, after `updateSquadAggregate`:

```cpp
                selectTargetSoldier(soldiers, squads, squadMembers, s);
```

This reads another squad's members' positions. That is safe here because soldier positions are written only in phases 4 and 7, so they are read-only for the whole of phase 2.

- [ ] **Step 5: Add accessors**

In `Simulation.hpp`:

```cpp
uint32_t squadTargetSoldier(size_t s) const { return squads.targetSoldier[s]; }
```

- [ ] **Step 6: Test, verify determinism, regenerate baseline, commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: squads acquire a target soldier for their archers

Spec 6.5. Archer range is 280px while a soldier's own neighbour query reaches
150px, so an archer cannot perceive its own best target. The squad sees the
field through squad centroids instead and hands one down.

Recomputed every tick rather than cached, because compaction renumbers
soldiers and a stored index goes stale the moment anyone dies. Selection takes
the lowest slotIndex in range, which is what makes officers preferentially
targeted without a special case."
```

---

## Task 7: Archers loose arrows

Fire intent, then arrow spawn with target lead and integer-milliradian spread.

**Files:**
- Modify: `c++/src/Projectiles.hpp`, `c++/src/Projectiles.cpp`, `c++/src/Simulation.cpp`, `c++/src/Rng.hpp`, `c++/src/Units.hpp`, `c++/tests/test_projectiles.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `selectTargetSoldier` from task 6, `ProjectileHot` from task 2.
- Produces: `void spawnArrows(const SoldierHot&, const SquadHot&, ProjectileHot&, const Rng&)` in `Projectiles.hpp`. New `RngUse` enumerators `ArrowSpread`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_projectiles.cpp`:

```cpp
TEST_CASE("an arrow leaves at arrow speed and roughly toward the target") {
    SoldierHot s;
    s.spawn(100.0f, 100.0f, 0, 0, Team::A, UnitType::Archer, 0);
    s.intentFire[0] = 1;

    SquadHot q;
    q.spawn(Team::A, UnitType::Archer);
    q.targetSoldier[0] = 1;
    s.spawn(300.0f, 100.0f, 0, 0, Team::B, UnitType::Infantry, 1);

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
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `spawnArrows` undeclared.

- [ ] **Step 3: Add the RngUse enumerator and spread constants**

In `Rng.hpp`, immediately before `Count`:

```cpp
    // Plan 2: archery
    ArrowSpread,
```

In `Units.hpp`:

```cpp
// Shot accuracy. Spread is carried in integer milliradians because Rng::range
// is integer-only; passing float bounds to it does not compile.
constexpr int   kArrowBaseSpreadMrad = 40;    // about 2.3 degrees at rest
constexpr float kArcherCooldown      = 1.5f;  // seconds between shots
```

- [ ] **Step 4: Implement `spawnArrows`**

Declare in `Projectiles.hpp`:

```cpp
// Resolution step 3 (spec 5.5). Converts intentFire flags into arrows, in
// ascending soldier index order so the projectile array's contents do not
// depend on thread scheduling.
void spawnArrows(const SoldierHot& soldiers, const SquadHot& squads,
                 ProjectileHot& out, const Rng& rng);
```

Implement in `Projectiles.cpp`:

```cpp
void spawnArrows(const SoldierHot& soldiers, const SquadHot& squads,
                 ProjectileHot& out, const Rng& rng) {
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (!soldiers.intentFire[i]) continue;

        const uint16_t sq = soldiers.squadId[i];
        if (sq >= squads.count) continue;
        const uint32_t t = squads.targetSoldier[sq];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;

        const float px = soldiers.posX[i];
        const float py = soldiers.posY[i];

        // Lead the target by its own flight time, so a moving target is aimed
        // where it will be rather than where it is.
        float dx = soldiers.posX[t] - px;
        float dy = soldiers.posY[t] - py;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float flight = dist / kArrowSpeed;
        dx += soldiers.velX[t] * flight;
        dy += soldiers.velY[t] * flight;

        const float aimLen = std::sqrt(dx * dx + dy * dy);
        if (aimLen < 1e-4f) continue;
        float ax = dx / aimLen;
        float ay = dy / aimLen;

        // Accuracy degrades with range and with the archer moving. Integer
        // milliradians throughout: Rng::range takes ints.
        const float shooterSpeed = std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                                             soldiers.velY[i] * soldiers.velY[i]);
        const float maxRange = kUnitStats[(int)UnitType::Archer].range;
        const float maxSpeed = kUnitStats[(int)UnitType::Archer].speed;
        int spreadMrad = (int)((float)kArrowBaseSpreadMrad
                             * (1.0f + dist / maxRange)
                             * (1.0f + shooterSpeed / maxSpeed));
        if (spreadMrad < 1) spreadMrad = 1;

        const int offMrad = rng.range((uint32_t)i, RngUse::ArrowSpread,
                                      -spreadMrad, spreadMrad);
        const float theta = (float)offMrad * 0.001f;

        // detmath, not libm: libm's sin is not bit-identical across platforms
        // and this feeds the state digest.
        const float st = detmath::sin(theta);
        const float ct = detmath::sin(theta + detmath::HALF_PI);  // cos
        const float rx = ax * ct - ay * st;
        const float ry = ax * st + ay * ct;

        out.spawn(px, py, rx * kArrowSpeed, ry * kArrowSpeed,
                  soldiers.team[i], kArrowDamage, kArrowLifetime);
    }
}
```

Include `"DetMath.hpp"`, `"Simulation.hpp"`, `"Squads.hpp"`, and `"Rng.hpp"` at the top.

- [ ] **Step 5: Set the fire intent in the steering phase**

In `phaseSoldierSteerChunk`, after melee selection:

```cpp
        // Archers fire at whatever their squad handed them, subject to cooldown.
        // Writing only our own flag keeps this parallel-safe; resolution turns
        // flags into arrows.
        soldiers.intentFire[i] = 0;
        if (soldiers.unitType[i] == UnitType::Archer &&
            soldiers.attackCooldown[i] <= 0.0f &&
            soldiers.state[i] != SoldierState::Dead) {
            const uint16_t sq = soldiers.squadId[i];
            if (sq < squads.count && squads.targetSoldier[sq] != UINT32_MAX) {
                soldiers.intentFire[i] = 1;
            }
        }
```

- [ ] **Step 6: Call `spawnArrows` from resolution and set the cooldown**

In `phaseResolution`, between steps 2 and 4:

```cpp
    spawnArrows(soldiers, squads, projectiles, rng);   // step 3
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.intentFire[i]) {
            soldiers.attackCooldown[i] = kArcherCooldown;
            soldiers.intentFire[i] = 0;
        }
    }
```

`phaseResolution` currently takes an unnamed `const Rng&`; name the parameter `rng` so it can be passed through.

- [ ] **Step 7: Test, verify determinism, regenerate baseline, commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: archers loose arrows that lead the target and can miss

Spread is expressed in integer milliradians because Rng::range is integer
only, and the rotation uses detmath rather than libm, since libm's sin is not
bit-identical across platforms and this value reaches the state digest.

Accuracy degrades with range and with the archer moving, so a stationary
archer at close range is genuinely dangerous and a running one is not.
Arrows are spawned in ascending soldier index order so the projectile array's
contents never depend on thread scheduling."
```

---

## Task 8: Arrows fly and hit

Integration plus the swept-segment hit test. The point test is not a viable starting point here and this task explains why in code.

**Files:**
- Modify: `c++/src/Projectiles.hpp`, `c++/src/Projectiles.cpp`, `c++/src/Simulation.cpp`, `c++/src/WorkCounters.hpp`, `c++/tests/test_projectiles.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `ProjectileHot` from task 2, `SpatialHash`.
- Produces: `bool segmentHitsCircle(float x0, float y0, float x1, float y1, float cx, float cy, float r)` and `void integrateProjectile(ProjectileHot&, const SoldierHot&, const SpatialHash&, size_t projectileIndex, float dt, std::vector<uint32_t>& scratch)` in `Projectiles.hpp`. `WorkCounters` gains `projectileHitTests`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_projectiles.cpp`:

```cpp
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

    // Neither endpoint is inside the circle.
    CHECK(std::sqrt((cx - x0) * (cx - x0)) > kSoldierRadius * 0.5f);
    // The swept test still finds it.
    CHECK(segmentHitsCircle(x0, y0, x1, y1, cx, cy, kSoldierRadius * 0.5f));
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
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `segmentHitsCircle` undeclared.

- [ ] **Step 3: Implement the swept test**

In `Projectiles.hpp`:

```cpp
// Closest-approach test between a segment and a circle. Used instead of
// sampling the arrow's endpoint each tick, because tunnelling depends on
// RELATIVE velocity, not arrow speed alone: an arrow at 200px/s against
// cavalry crossing at 95px/s covers about 4.9px of relative displacement per
// tick against a 4px radius, so a point test would let arrows pass straight
// through a galloping target.
bool segmentHitsCircle(float x0, float y0, float x1, float y1,
                       float cx, float cy, float r);
```

In `Projectiles.cpp`:

```cpp
bool segmentHitsCircle(float x0, float y0, float x1, float y1,
                       float cx, float cy, float r) {
    const float sx = x1 - x0;
    const float sy = y1 - y0;
    const float lenSq = sx * sx + sy * sy;

    float t = 0.0f;
    if (lenSq > 1e-12f) {
        // Project the centre onto the segment, clamped to its ends.
        t = ((cx - x0) * sx + (cy - y0) * sy) / lenSq;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    const float nearestX = x0 + sx * t;
    const float nearestY = y0 + sy * t;
    const float dx = cx - nearestX;
    const float dy = cy - nearestY;
    return dx * dx + dy * dy <= r * r;
}
```

- [ ] **Step 4: Implement integration**

```cpp
void integrateProjectile(ProjectileHot& p, const SoldierHot& soldiers,
                         const SpatialHash& hash, size_t i, float dt,
                         std::vector<uint32_t>& scratch) {
    p.intentHitTarget[i] = UINT32_MAX;
    p.lifetime[i] -= dt;
    if (p.lifetime[i] <= 0.0f) return;

    const float x0 = p.posX[i];
    const float y0 = p.posY[i];
    const float x1 = x0 + p.velX[i] * dt;
    const float y1 = y0 + p.velY[i] * dt;
    p.posX[i] = x1;
    p.posY[i] = y1;

    // Query around the segment's midpoint with a radius covering half its
    // length plus the soldier radius, so nothing along the path is missed.
    const float midX = (x0 + x1) * 0.5f;
    const float midY = (y0 + y1) * 0.5f;
    const float half = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) * 0.5f;
    hash.queryNeighbors(midX, midY, half + kSoldierRadius, scratch);

    uint32_t best = UINT32_MAX;
    for (uint32_t n : scratch) {
        if (soldiers.team[n] == p.team[i]) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;
        if (!segmentHitsCircle(x0, y0, x1, y1,
                               soldiers.posX[n], soldiers.posY[n], kSoldierRadius)) {
            continue;
        }
        // Lowest index wins any tie, so the outcome does not depend on the
        // order the spatial hash happened to return candidates in.
        if (n < best) best = n;
    }
    p.intentHitTarget[i] = best;
}
```

- [ ] **Step 5: Dispatch phase 5 and add the counter**

In `WorkCounters.hpp`, add `std::atomic<uint64_t> projectileHitTests{0};` and reset it alongside the others.

Replace the `phaseProjectiles` stub in `Simulation.cpp`:

```cpp
void Simulation::phaseProjectiles(float dt) {
    const size_t chunkSize = 128;
    for (size_t start = 0; start < projectiles.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, projectiles.count);
        jobSystem.submit([this, start, end, dt]() {
            std::vector<uint32_t> scratch;
            scratch.reserve(64);
            for (size_t i = start; i < end; ++i) {
                integrateProjectile(projectiles, soldiers, spatialHash, i, dt, scratch);
                workCounters.add(workCounters.projectileHitTests, 1);
            }
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
}
```

- [ ] **Step 6: Gate on the new counter**

In `c++/tests/test_counters.cpp`, add `projectileHitTests` to `CounterSnapshot`, `snapshot()`, and every comparison, following the pattern the other five use exactly. Add a `projectileHitTests=` line to the baseline.

- [ ] **Step 7: Test, verify determinism, regenerate baseline, commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: arrows integrate and hit-test with a swept segment

Tunnelling depends on relative velocity, not arrow speed alone. An arrow at
200px/s against cavalry crossing at 95px/s covers about 4.9px of relative
displacement per tick against a 4px radius, so sampling the endpoint each
tick would let arrows pass straight through a galloping target. The hit test
is therefore a segment against a circle.

Ties break on lowest soldier index so the outcome does not depend on the
order the spatial hash returned candidates in."
```

---

## Task 9: Arrow hits are resolved

The last resolution step, plus removal of spent arrows.

**Files:**
- Modify: `c++/src/Projectiles.hpp`, `c++/src/Projectiles.cpp`, `c++/src/Simulation.cpp`, `c++/tests/test_projectiles.cpp`, `c++/tests/baseline/counters-2k-200.txt`

**Interfaces:**
- Consumes: `integrateProjectile` from task 8.
- Produces: `void applyProjectileHits(ProjectileHot&, SoldierHot&)` and `void compactProjectiles(ProjectileHot&)` in `Projectiles.hpp`.

- [ ] **Step 1: Write the failing test**

Append to `c++/tests/test_projectiles.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL to compile, `applyProjectileHits` undeclared.

- [ ] **Step 3: Implement both**

```cpp
void applyProjectileHits(ProjectileHot& p, SoldierHot& soldiers) {
    for (size_t i = 0; i < p.count; ++i) {
        const uint32_t t = p.intentHitTarget[i];
        if (t == UINT32_MAX || (size_t)t >= soldiers.count) continue;
        if (soldiers.health[t] == 0) continue;  // no underflow, no overkill

        soldiers.health[t] = (soldiers.health[t] > p.damage[i])
                           ? (uint8_t)(soldiers.health[t] - p.damage[i])
                           : (uint8_t)0;

        // An arrow that lands is spent. Marking it expired lets one compaction
        // pass remove both hits and misses that ran out of flight time.
        p.lifetime[i] = 0.0f;
        p.intentHitTarget[i] = UINT32_MAX;
    }
}

void compactProjectiles(ProjectileHot& p) {
    size_t i = 0;
    while (i < p.count) {
        if (p.lifetime[i] > 0.0f) {
            ++i;
            continue;
        }
        const size_t last = p.count - 1;
        if (i != last) {
            p.posX[i] = p.posX[last];
            p.posY[i] = p.posY[last];
            p.velX[i] = p.velX[last];
            p.velY[i] = p.velY[last];
            p.team[i] = p.team[last];
            p.damage[i] = p.damage[last];
            p.lifetime[i] = p.lifetime[last];
            p.intentHitTarget[i] = p.intentHitTarget[last];
            // Do not advance i: the entry swapped in is unexamined.
        } else {
            ++i;
        }
        p.posX.pop_back();
        p.posY.pop_back();
        p.velX.pop_back();
        p.velY.pop_back();
        p.team.pop_back();
        p.damage.pop_back();
        p.lifetime.pop_back();
        p.intentHitTarget.pop_back();
        p.count--;
    }
}
```

- [ ] **Step 4: Wire into resolution in the spec's order**

```cpp
void Simulation::phaseResolution(const Rng& rng) {
    casualties.assign(squads.count, 0u);
    officerDied.assign(squads.count, 0u);

    applyMeleeIntents(soldiers);                            // step 1
    applyProjectileHits(projectiles, soldiers);             // step 2
    spawnArrows(soldiers, squads, projectiles, rng);        // step 3
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.intentFire[i]) {
            soldiers.attackCooldown[i] = kArcherCooldown;
            soldiers.intentFire[i] = 0;
        }
    }
    recordCasualties(soldiers, casualties, officerDied);    // step 4
    // step 5 (morale and discipline) is plan 3
    compactProjectiles(projectiles);
    compactDead(soldiers, prevPosX, prevPosY);              // step 6
    rebuildSquadMembers(soldiers, squads, squadMembers);    // step 7
}
```

Note that step 2 must precede step 3, so an arrow spawned this tick is not hit-tested before it has flown, and both must precede step 6, because both hold soldier indices that compaction invalidates.

- [ ] **Step 5: Test, verify determinism, regenerate baseline, commit**

```bash
git add c++/src c++/tests c++/tests/baseline/counters-2k-200.txt
git commit -m "feat: resolve arrow hits and remove spent arrows

Resolution step 2, plus projectile compaction. The health guard makes an
arrow unable to finish off an already dead soldier, which prevents both
overkill and a uint8_t underflow to 255.

Hits are applied before new arrows spawn, so an arrow cannot be hit-tested on
the tick it was loosed, and both run before compaction, since both hold
soldier indices that compaction invalidates."
```

---

## Task 10: Render the fight

Arrows in flight and the visible consequence of damage. Tactical overlays remain plan 4.

**Files:**
- Modify: `c++/src/Renderer.cpp`, `c++/src/main.cpp`

**Interfaces:**
- Consumes: `ProjectileHot`, `SoldierHot::health`.
- Produces: no new API.

- [ ] **Step 1: Draw arrows as segments along their velocity**

In `Renderer.cpp`, after the soldier loop:

```cpp
    // Arrows are drawn along their velocity rather than as dots, so a volley
    // reads as direction and not as speckle.
    for (size_t i = 0; i < sim.projectiles.count; ++i) {
        const float vx = sim.projectiles.velX[i];
        const float vy = sim.projectiles.velY[i];
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < 1e-4f) continue;
        const float sx = sim.projectiles.posX[i];
        const float sy = sim.projectiles.posY[i];
        const float ex = sx - (vx / len) * 6.0f;
        const float ey = sy - (vy / len) * 6.0f;
        DrawLineV(Vector2{sx, sy}, Vector2{ex, ey}, Color{235, 225, 190, 255});
    }
```

- [ ] **Step 2: Dim wounded soldiers**

Inside the soldier colour block, after the team and unit-type colour is chosen:

```cpp
    // Health reads as brightness, so a worn-down line is visible before it
    // breaks rather than only when it vanishes.
    const uint8_t maxHp = kUnitStats[(int)sim.soldiers.unitType[i]].maxHealth;
    if (maxHp > 1) {
        const float frac = 0.45f + 0.55f * ((float)sim.soldiers.health[i] / (float)maxHp);
        agentColor.r = (uint8_t)(agentColor.r * frac);
        agentColor.g = (uint8_t)(agentColor.g * frac);
        agentColor.b = (uint8_t)(agentColor.b * frac);
    }
```

- [ ] **Step 3: Report the fight in the overlay**

In `main.cpp`, add to the ImGui panel:

```cpp
        ImGui::Text("Arrows in flight: %zu", sim.getProjectileCount());
```

- [ ] **Step 4: Run it and confirm by eye**

```bash
cd c++ && cmake --build build --config Release --parallel && ./build/Release/tactix.exe
```

Expected: the two armies advance, meet, and fight. Arrows are visible crossing the gap before contact. Soldiers dim as they take damage and disappear when killed. Report what you actually observed; if you cannot run a GUI, say so rather than claiming you saw it.

- [ ] **Step 5: Measure and commit**

```bash
cd c++ && ./build/Release/tactix_bench.exe --agents 10000 --ticks 2000 --seed 42 --width 4000 --height 2500 --json
```

Record p50, p95, p99, and max. Combat adds real work per tick, so expect these above plan 1's figures; the question is whether they stay inside the 16.67ms budget. Do not publish them in the README; plan 4 does that.

```bash
git add c++/src
git commit -m "feat: render arrows and damage

Arrows draw along their velocity so a volley reads as direction rather than
speckle, and health reads as brightness so a worn-down line is visible before
it breaks rather than only when it vanishes.

Tactical overlays are plan 4."
```

---

## Definition of Done

- [ ] `ctest --test-dir build -C Release --output-on-failure` passes on Linux and Windows
- [ ] `stateDigest()` identical at 1, 2, 4, and 8 worker threads
- [ ] The ThreadSanitizer CI job passes, gating (it no longer carries continue-on-error)
- [ ] `tactix_sim` links no raylib symbols
- [ ] Two armies advance, make contact, and soldiers die
- [ ] Archers acquire targets beyond their own perception radius and loose arrows that travel and can miss
- [ ] Arrows hit a crossing target that a point test would tunnel through
- [ ] Soldier count decreases over a battle, and the projectile array does not grow without bound
- [ ] `casualties[]` and `officerDied[]` are populated for plan 3 to consume
- [ ] No em dashes or en dashes, and no AI attribution, in any commit on the branch

Verify the last item with:

```bash
git log master..HEAD --format='%h%n%B' | grep -nP '\xe2\x80\x94|\xe2\x80\x93|claude|anthropic|co-authored' -i
```

Expected: no output.
