# Engagement and Unit AI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make infantry formations hold a real line at contact, make squads coordinate through an army tier, and give archers firing discipline, friendly-fire awareness, and flight.

**Architecture:** Four layers stacked from local to global. Contact and cohesion cut the centroid/slot feedback loop that currently makes melee a rotating blob, and add a non-penetration pass so bodies stop bodies. Morale turns already-recorded casualty data into a cohesion and rout signal. A two-entity army tier assigns squads roles and targets so screening and target spreading are structural. Archery gets an arced arrow, a settle-time accuracy model, and a panic response.

**Tech Stack:** C++17, CMake, doctest, custom job system, structure-of-arrays entity storage, spdlog. No new dependencies.

**Spec:** [`docs/superpowers/specs/2026-08-22-engagement-and-unit-ai-design.md`](../specs/2026-08-22-engagement-and-unit-ai-design.md)

## Global Constraints

These apply to every task without exception. Violating any of them is a rejected task, not a style note.

- **Thread-count invariance is load-bearing.** `Simulation::stateDigest()` must be bit-identical at 1, 2, 8, and 15 worker threads. Any parallel phase must have each agent write only its own state and read only data that is read-only for that phase's whole duration.
- **Cross-agent mutation happens only in serial `phaseResolution`.** Nowhere else, ever.
- **Iterate in ascending index order. Break every tie on the lower index.** `std::sort` is introsort, not stable: any comparator must be a strict total order, tie-broken on a unique key.
- **Use `detmath::sin`, never `std::sin`/`std::cos`,** for anything that reaches the digest. `std::sqrt` is fine (IEEE-754 pins it down). See `src/DetMath.hpp`.
- **Enum values are appended, never renumbered.** `SquadOrder`, `SquadRole`, and `RngUse` all feed the digest or the RNG stream.
- **New per-soldier work is O(neighbors) through `SpatialHash`,** never O(n squared) over soldiers. O(squads squared) is acceptable inside a phase that already loops all squads.
- **No em dashes** in code comments, docs, or commit messages. Restructure the sentence instead.
- **No AI co-author trailer** on any commit.
- **Every new field added to `SoldierHot`, `SquadHot`, `ProjectileHot`, or `ArmyHot` must be:** initialized in that struct's `spawn()`, moved by its compaction routine if it has one, and mixed into `stateDigest()`. Task 20 verifies this; do not defer it there.

**Build and test commands** (run from `c++/`, Git Bash or cmd both work):

```bash
scripts/build.bat -t
```

Single test case:

```bash
build/Release/tactix_tests.exe -tc="name of the test case"
```

Benchmark (regenerating baselines):

```bash
build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

---

## File Structure

**New source files** (all must be added to the `tactix_sim` target in `c++/CMakeLists.txt`, which uses an explicit source list rather than a glob):

| File | Responsibility |
|---|---|
| `src/Contact.hpp` / `.cpp` | Front-rank contact detection, anchor latching, and the non-penetration pass. Everything about two bodies meeting. |
| `src/Morale.hpp` / `.cpp` | Morale update and rout entry/exit. Pure functions over `SquadHot`, called from serial resolution. |
| `src/Army.hpp` / `.cpp` | `ArmyHot`, army aggregates and front line, role and target assignment. |

**New test files** (added to the `tactix_tests` target):

| File | Covers |
|---|---|
| `tests/test_contact.cpp` | Tasks 3, 4, 5 |
| `tests/test_morale.cpp` | Tasks 7, 8 |
| `tests/test_army.cpp` | Tasks 9, 10, 11 |

**Modified files:**

| File | Change |
|---|---|
| `src/Units.hpp` | New orders, roles, constants. |
| `src/Formation.hpp` | `rankOfSlot` helper, cohesion compression. |
| `src/Squads.hpp` / `.cpp` | New fields, facing slew, commander-driven targeting, role anchors, new scorer terms. |
| `src/Soldiers.hpp` / `.cpp` | Anchor-relative slots, compression, flee speed. |
| `src/Projectiles.hpp` / `.cpp` | Arrow arc, both-team hit testing, settle-time spread. |
| `src/Simulation.hpp` / `.cpp` | Phase wiring, `nextPos` scratch, digest coverage. |
| `src/WorkCounters.hpp` | `armyDecisions` counter. |
| `tests/test_counters.cpp` + `tests/baseline/*.txt` | Seventh counter key, regenerated values. |
| `README.md` | Republished tick figures and digest. |

**Two deviations from the spec, both deliberate simplifications. Implement the plan's version:**

1. The spec names `kContactClearTicks` and `kAnchorReleaseTicks`. This plan uses **seconds** (`kContactClearSeconds`, `kAnchorReleaseSeconds`) so the constants stay correct if the timestep ever changes.
2. The spec describes `slotWorldPosition` switching between `anchor` and `centroid`. This plan makes it **always** read `anchor`, and has `anchor` track `centroid` exactly whenever the squad is not engaged or releasing. One code path instead of two, and it removes the position jump at contact release for free.

---

## Task 1: Rank derivation helper

`Formation.hpp` knows how `formationSlot` lays out ranks, but nothing can currently ask "which rank is this slot in?" Contact detection (Task 3) needs that, and the answer is shape-dependent: `Wedge` ranks by `floor(sqrt(slotIndex))`, everything else by `slotIndex / rankWidth`. A single formula would silently break cavalry.

**Files:**
- Modify: `c++/src/Formation.hpp`
- Test: `c++/tests/test_formation.cpp`

**Interfaces:**
- Consumes: `detail::rankWidth`, `formationSlot`, `FormationShape` (all existing in `Formation.hpp`).
- Produces: `uint32_t rankOfSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount)`. Task 3 uses it to find front-rank members; Task 6 uses it for nothing (compression scales the offset directly), so this is its only consumer.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_formation.cpp`:

```cpp
TEST_CASE("rankOfSlot agrees with the forward offset formationSlot produces") {
    // The rank a slot belongs to is observable from formationSlot's output:
    // rank r sits at forward = -r * spacing. Deriving the expected value from
    // the function under test's own sibling is what makes this a consistency
    // check rather than a restatement of the implementation.
    struct Case { FormationShape shape; float spacing; };
    const Case cases[] = {
        { FormationShape::Line,   kSlotSpacing },
        { FormationShape::Column, kSlotSpacing },
        { FormationShape::Wedge,  kSlotSpacing },
        { FormationShape::Loose,  kSlotSpacing * 2.0f },
    };

    for (const Case& c : cases) {
        for (uint32_t n : { 1u, 2u, 7u, 25u, 60u }) {
            for (uint16_t i = 0; i < (uint16_t)n; ++i) {
                const Vec2 s = formationSlot(c.shape, i, n);
                const uint32_t expected = (uint32_t)(-s.y / c.spacing + 0.5f);
                CHECK(rankOfSlot(c.shape, i, n) == expected);
            }
        }
    }
}

TEST_CASE("rankOfSlot puts slot 0 in the front rank for every shape") {
    for (FormationShape shape : { FormationShape::Line, FormationShape::Column,
                                  FormationShape::Wedge, FormationShape::Loose }) {
        CHECK(rankOfSlot(shape, 0, 40) == 0u);
    }
}

TEST_CASE("rankOfSlot handles an empty squad without dividing by zero") {
    CHECK(rankOfSlot(FormationShape::Line, 0, 0) == 0u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `'rankOfSlot': identifier not found`.

- [ ] **Step 3: Write minimal implementation**

Add to `c++/src/Formation.hpp`, directly after `formationSlot` so the two layout rules sit adjacent and cannot drift apart:

```cpp
// Which rank a slot belongs to, rank 0 being the front. This MUST mirror
// formationSlot's own layout: Wedge packs rank r into slots r*r .. r*r+2r, so
// its rank is floor(sqrt(i)), while the grid shapes rank by integer division
// on the same width rankWidth computes. Kept next to formationSlot precisely
// so a change to one is an obvious prompt to change the other. Contact
// detection (Contact.cpp) is the consumer: it tests only the front rank, and a
// wrong rank here would silently make a whole squad or none of it eligible.
inline uint32_t rankOfSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount) {
    if (memberCount == 0) return 0;

    if (shape == FormationShape::Wedge) {
        return (uint32_t)std::sqrt((float)slotIndex);
    }

    // Loose differs from Line only in spacing, not in aspect, so it shares
    // this branch. Column is the narrow-and-deep aspect.
    const float aspect = (shape == FormationShape::Column) ? 0.5f : 2.0f;
    const uint32_t width = detail::rankWidth(memberCount, aspect);
    return (uint32_t)slotIndex / width;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: PASS, all three new cases green, no existing case regressed.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Formation.hpp c++/tests/test_formation.cpp
git commit -m "feat: add rankOfSlot, the shape-aware inverse of formationSlot

Contact detection needs to identify front-rank members, and rank is not
derivable by one formula: Wedge packs rank r into slots r*r..r*r+2r while
the grid shapes divide by rankWidth. Lives beside formationSlot so the two
layout rules cannot drift."
```

---

## Task 2: Facing slew limit

Facing currently snaps to the normalized centroid-to-centroid vector every tick. When two squads' centroids nearly coincide, that vector is near zero length and flips sign on tiny numeric changes, which snaps every formation slot around with it. This is the visible half of the spin (spec 2.1). Limiting the turn rate removes it as an independent guard, and makes every formation turn read as a maneuver rather than a snap.

**Files:**
- Modify: `c++/src/Units.hpp`, `c++/src/Squads.hpp`, `c++/src/Squads.cpp`, `c++/src/Simulation.cpp`
- Test: `c++/tests/test_squads.cpp`

**Interfaces:**
- Consumes: `detmath::sin`, `detmath::HALF_PI` from `src/DetMath.hpp`.
- Produces: `Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians)`, declared in `Squads.hpp`. Rotates `current` toward `desired` by at most `maxRadians` and returns a unit vector. Task 11 calls it from the rewritten squad decide.
- Changes: `void selectTargetSquad(SquadHot&, size_t, const TerrainField&)` gains a trailing `float dt` parameter. `Simulation::phaseSquadDecide(const Rng&)` gains a leading `float dt` parameter. Task 11 replaces this function wholesale; the signature it establishes here is what Task 11 keeps.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_squads.cpp`:

```cpp
TEST_CASE("slewFacing snaps when the desired facing is within one step") {
    const Vec2 cur{ 1.0f, 0.0f };
    const Vec2 want{ 0.0f, 1.0f };          // 90 degrees away
    const Vec2 got = slewFacing(cur, want, 2.0f);  // 2 rad > pi/2, so snap
    CHECK(got.x == doctest::Approx(want.x).epsilon(1e-5));
    CHECK(got.y == doctest::Approx(want.y).epsilon(1e-5));
}

TEST_CASE("slewFacing turns by exactly the step when the target is farther") {
    const Vec2 cur{ 1.0f, 0.0f };
    const Vec2 want{ 0.0f, 1.0f };          // 90 degrees, counter-clockwise
    const float step = 0.1f;
    const Vec2 got = slewFacing(cur, want, step);

    // Result must still be unit length, and exactly `step` radians around.
    CHECK(std::sqrt(got.x * got.x + got.y * got.y) == doctest::Approx(1.0f).epsilon(1e-5));
    const float dot = cur.x * got.x + cur.y * got.y;   // = cos(step)
    CHECK(dot == doctest::Approx(detmath::sin(step + detmath::HALF_PI)).epsilon(1e-5));
    CHECK(got.y > 0.0f);   // turned toward `want`, not away from it
}

TEST_CASE("slewFacing turns the short way round in both directions") {
    const Vec2 cur{ 1.0f, 0.0f };
    const Vec2 ccw = slewFacing(cur, Vec2{ 0.0f,  1.0f }, 0.1f);
    const Vec2 cw  = slewFacing(cur, Vec2{ 0.0f, -1.0f }, 0.1f);
    CHECK(ccw.y > 0.0f);
    CHECK(cw.y  < 0.0f);
}

TEST_CASE("slewFacing never produces NaN from a degenerate input") {
    // A near-zero desired vector is exactly the case that makes the old
    // snap-to-target facing flip sign every tick, so it must be safe here.
    const Vec2 cur{ 1.0f, 0.0f };
    const Vec2 got = slewFacing(cur, Vec2{ 0.0f, 0.0f }, 0.1f);
    CHECK(got.x == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(got.y == doctest::Approx(0.0f).epsilon(1e-5));

    const Vec2 fromZero = slewFacing(Vec2{ 0.0f, 0.0f }, Vec2{ 0.0f, 1.0f }, 0.1f);
    CHECK(std::sqrt(fromZero.x * fromZero.x + fromZero.y * fromZero.y)
          == doctest::Approx(1.0f).epsilon(1e-5));
}
```

Add `#include "DetMath.hpp"` to the test file's includes.

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `'slewFacing': identifier not found`.

- [ ] **Step 3: Write the implementation**

Add the constant to `c++/src/Units.hpp`, near `kAdvanceLead`:

```cpp
// How fast a squad may rotate its formation, in radians per second. Facing
// rotates every slot, so an unbounded turn teleports the whole formation.
// It is also the guard against spec 2.1's spin: two squads whose centroids
// nearly coincide produce a near-zero facing vector that flips sign on tiny
// numeric changes, and a rate limit turns that flip into a slow sweep no
// matter what the vector does. About 143 degrees per second: fast enough to
// answer a flank, slow enough to read as a maneuver.
constexpr float kFacingSlewRate = 2.5f;
```

Declare in `c++/src/Squads.hpp`:

```cpp
// Rotates `current` toward `desired` by at most `maxRadians`, returning a unit
// vector. Falls back to `current` (normalized) when `desired` is degenerate,
// and to (1,0) when both are, so this never produces NaN.
//
// Deliberately avoids atan2: there is no deterministic atan2 in DetMath, and
// none is needed. The dot product answers "are we within one step" and the
// cross product answers "which way", which is the whole decision.
Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians);
```

Implement in `c++/src/Squads.cpp` (add `#include "DetMath.hpp"`):

```cpp
Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians) {
    const Vec2 cur  = normalizeSafe(current, Vec2{ 1.0f, 0.0f });
    // A degenerate desired direction means "no opinion", so hold current
    // facing rather than inventing one. This is the near-coincident-centroid
    // case, and holding is exactly the right answer for it.
    const float wantLen = std::sqrt(desired.x * desired.x + desired.y * desired.y);
    if (wantLen < 1e-6f) return cur;
    const Vec2 want{ desired.x / wantLen, desired.y / wantLen };

    const float dot   = cur.x * want.x + cur.y * want.y;
    const float cross = cur.x * want.y - cur.y * want.x;

    // detmath, not libm: facing rotates every formation slot and therefore
    // reaches the state digest. cos(t) is sin(t + pi/2).
    const float c = detmath::sin(maxRadians + detmath::HALF_PI);
    const float s = detmath::sin(maxRadians);

    // dot >= cos(step) means the angle between them is at most `step`, so we
    // can arrive this tick. Snapping here rather than always rotating is what
    // stops a settled squad jittering around its target facing forever.
    if (dot >= c) return want;

    // Rotate by `step` in the direction of the cross product's sign. At
    // exactly 180 degrees the cross product is zero and this picks
    // counter-clockwise, arbitrarily but deterministically, which is all
    // that matters: both are equally short.
    const float sgn = (cross >= 0.0f) ? 1.0f : -1.0f;
    const float ss = s * sgn;
    return Vec2{ cur.x * c - cur.y * ss, cur.x * ss + cur.y * c };
}
```

Now use it. In `c++/src/Squads.cpp`, change `selectTargetSquad`'s signature to take `float dt` and replace the facing assignment block:

```cpp
        // Facing comes from the order's objective (spec 6.6), but it is SLEWED
        // rather than assigned. See kFacingSlewRate for why the rate limit is
        // a correctness guard and not just polish.
        const float dx = squads.centroidX[best] - squads.centroidX[s];
        const float dy = squads.centroidY[best] - squads.centroidY[s];
        const Vec2 f = slewFacing(Vec2{ squads.facingX[s], squads.facingY[s] },
                                  Vec2{ dx, dy }, kFacingSlewRate * dt);
        squads.facingX[s] = f.x;
        squads.facingY[s] = f.y;
```

Update the declaration in `c++/src/Squads.hpp` to match, then thread `dt` through `Simulation`: change `void phaseSquadDecide(const Rng& rng)` to `void phaseSquadDecide(float dt, const Rng& rng)` in `Simulation.hpp` and `.cpp`, pass `dt` to `selectTargetSquad`, and update the call in `tick()` to `phaseSquadDecide(dt, rng)`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: the four new cases PASS. `test_counters.cpp` FAILS on `stateDigest`, because facing now changes gradually and facing rotates every slot. That failure is expected and is fixed in Step 5, not by reverting.

- [ ] **Step 5: Regenerate the two counter baselines**

Run both:

```bash
build/Release/tactix_bench.exe --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

```bash
build/Release/tactix_bench.exe --agents 2000 --ticks 700 --seed 42 --threads 1 --json
```

Paste each run's six counters and hex `stateDigest` into `c++/tests/baseline/counters-2k-200.txt` and `counters-2k-700.txt` respectively. Replace the "Regenerated ..." paragraph at the top of each file with:

```
# Regenerated 2026-08-22: squad facing is now rate-limited (kFacingSlewRate)
# instead of snapping to the enemy direction each tick. Facing rotates every
# formation slot, so this changes where every soldier is told to stand and
# therefore the whole battle. Deliberate behavioural change, not a bug.
```

Re-run `scripts/build.bat -t` and confirm green.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Units.hpp c++/src/Squads.hpp c++/src/Squads.cpp \
        c++/src/Simulation.hpp c++/src/Simulation.cpp \
        c++/tests/test_squads.cpp c++/tests/baseline/
git commit -m "feat: rate-limit squad facing instead of snapping it

Facing was assigned from the normalized centroid-to-centroid vector every
tick. When two squads' centroids nearly coincide that vector is near zero
length and flips sign on tiny numeric changes, snapping every formation
slot around with it. That is the visible half of the melee spin.

slewFacing turns by at most kFacingSlewRate per second, using dot and cross
products rather than atan2 so it stays inside DetMath's guarantees.

Baselines regenerate: facing rotates every slot, so this moves the digest."
```

---

## Task 3: Front-rank contact detection

A squad needs to know when its front rank has met the enemy, because that is the moment it must stop advancing. Detection runs in `phaseSquadAggregate`, which already walks each squad's members.

**The determinism hazard here is specific and easy to trip.** That phase runs parallel across squads, and every squad's centroid is being written by its own concurrent job. Contact detection may read **soldier positions** (finalized last tick, read-only all tick) and the spatial hash. It may **not** read another squad's centroid. This is the identical hazard `updateSquadAggregate`'s existing comment documents for facing.

**Files:**
- Create: `c++/src/Contact.hpp`, `c++/src/Contact.cpp`, `c++/tests/test_contact.cpp`
- Modify: `c++/src/Units.hpp`, `c++/src/Squads.hpp`, `c++/src/Simulation.cpp`, `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `rankOfSlot` (Task 1), `shapeForUnit` and `kMeleeReach` (`Units.hpp`), `SpatialHash::queryNeighbors`, `SquadHot::memberStart`/`memberCount`, the `squadMembers` index.
- Produces: `void detectContact(const SoldierHot&, SquadHot&, const std::vector<uint32_t>& members, const SpatialHash&, size_t squadIndex, float dt, std::vector<uint32_t>& scratch)`. Sets `squads.contact[s]` and `squads.contactTimer[s]`. Task 4 latches the anchor inside this same function; Task 6 reads `contact` for compression; Task 11 reads it to pick the `Engaged` order.
- Adds to `SquadHot`: `std::vector<uint8_t> contact`, `std::vector<float> contactTimer`.

- [ ] **Step 1: Add the fields and constants**

In `c++/src/Squads.hpp`, add to `SquadHot` (and initialize both in `spawn()`, which is a Global Constraint):

```cpp
    // Contact state (design 5.1). `contact` is 1 while this squad's front rank
    // is engaged; `contactTimer` counts DOWN the grace period before contact
    // is allowed to clear, so a squad does not flicker between engaged and
    // advancing as individual enemies die.
    std::vector<uint8_t> contact;
    std::vector<float>   contactTimer;
```

In `spawn()`:

```cpp
        contact.push_back(0);
        contactTimer.push_back(0.0f);
```

In `c++/src/Units.hpp`, after the melee constants:

```cpp
// Contact detection (design 5.1). kContactRadius sits deliberately ABOVE
// kMeleeReach so a squad registers contact just BEFORE its front rank can
// swing: halting on the same frame as the first blow would let the formation
// overrun by a stride first.
constexpr float kContactRadius = kMeleeReach * 1.4f;   // 19.6px

// Fraction of the front rank that must have an enemy in reach. A single
// over-eager skirmisher must not halt a whole formation, and requiring the
// whole rank would never fire on a ragged line.
constexpr float kContactFraction = 0.25f;

// Grace period before contact is allowed to clear. Without it a squad
// flickers between Engaged and Advance every time a front-rank duel ends.
constexpr float kContactClearSeconds = 0.75f;
```

- [ ] **Step 2: Write the failing test**

Create `c++/tests/test_contact.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Contact.hpp"
#include "Squads.hpp"
#include "Simulation.hpp"
#include "SpatialHash.hpp"
#include "Formation.hpp"
#include <cmath>
#include <vector>

namespace {

// A minimal two-squad fixture: squad 0 is team A infantry, squad 1 is team B
// infantry, each with `n` members laid out in a straight line along x at the
// given origin. Bypasses Simulation so contact can be tested in isolation.
struct Fixture {
    SoldierHot soldiers;
    SquadHot squads;
    std::vector<uint32_t> members;
    SpatialHash hash{ 1280.0f, 720.0f, 50.0f };

    void addSquad(Team team, float originX, float originY, uint32_t n, float spacing) {
        const uint16_t sq = (uint16_t)squads.count;
        squads.spawn(team, UnitType::Infantry);
        squads.memberStart[sq] = (uint32_t)members.size();
        squads.memberCount[sq] = n;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t idx = (uint32_t)soldiers.count;
            soldiers.spawn(originX + (float)k * spacing, originY,
                           0.0f, 0.0f, team, UnitType::Infantry, sq);
            soldiers.slotIndex[idx] = (uint16_t)k;
            members.push_back(idx);
        }
    }

    void rehash() {
        hash.clear();
        for (size_t i = 0; i < soldiers.count; ++i) {
            hash.insert((uint32_t)i, soldiers.posX[i], soldiers.posY[i]);
        }
    }
};

} // namespace

TEST_CASE("a squad with no enemy nearby is not in contact") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 600.0f, 100.0f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}

TEST_CASE("a squad whose front rank meets the enemy enters contact") {
    Fixture f;
    // Two lines facing each other, well inside kContactRadius.
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 1);
}

TEST_CASE("one lone skirmisher in reach does not put a whole squad in contact") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 20, kSlotSpacing);
    // A single enemy, adjacent to exactly one member of a 20-man squad. One in
    // twenty is below kContactFraction, so the formation keeps marching.
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 1, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}

TEST_CASE("contact does not clear until the grace period expires") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);

    // Kill the enemy squad outright, then tick. Contact must persist through
    // the grace period and only then clear.
    for (uint32_t k = 0; k < f.squads.memberCount[1]; ++k) {
        f.soldiers.state[f.members[f.squads.memberStart[1] + k]] = SoldierState::Dead;
    }

    const int graceTicks = (int)(kContactClearSeconds / dt);
    for (int t = 0; t < graceTicks - 1; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 1);   // still latched

    for (int t = 0; t < 3; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 0);   // grace expired
}

TEST_CASE("an empty squad is never in contact and does not divide by zero") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 0, kSlotSpacing);
    f.rehash();

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);
    CHECK(f.squads.contact[0] == 0);
}
```

Add `tests/test_contact.cpp` to the `tactix_tests` source list in `c++/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, cannot open source file `Contact.hpp`.

- [ ] **Step 4: Write the implementation**

Create `c++/src/Contact.hpp`:

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;   // defined in Simulation.hpp
struct SquadHot;     // defined in Squads.hpp
class SpatialHash;

// Sets squad `s`'s contact flag from whether its FRONT RANK has enemies in
// reach, and latches its formation anchor on the rising edge (design 5.1, 5.2).
//
// Parallel-safe inside phaseSquadAggregate, but only because of what it reads:
// soldier positions (finalized last tick, read-only for this whole tick) and
// the spatial hash. It MUST NOT read any other squad's centroid, because every
// squad's centroid is being written by its own concurrent job in that same
// phase. This is the identical hazard updateSquadAggregate documents for
// facing, and it is why contact is measured against soldiers rather than
// against squads.
//
// `scratch` is a caller-owned neighbour buffer, reused across squads so this
// does not heap-allocate per squad per tick.
void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t squadIndex, float dt,
                   std::vector<uint32_t>& scratch);
```

Create `c++/src/Contact.cpp`:

```cpp
#include "Contact.hpp"
#include "Simulation.hpp"
#include "Squads.hpp"
#include "SpatialHash.hpp"
#include "Formation.hpp"
#include <cmath>

void detectContact(const SoldierHot& soldiers, SquadHot& squads,
                   const std::vector<uint32_t>& members,
                   const SpatialHash& hash, size_t s, float dt,
                   std::vector<uint32_t>& scratch) {
    const uint32_t start = squads.memberStart[s];
    const uint32_t n     = squads.memberCount[s];

    if (n == 0) {
        squads.contact[s] = 0;
        squads.contactTimer[s] = 0.0f;
        return;
    }

    const FormationShape shape = shapeForUnit(squads.unitType[s]);
    const Team ownTeam = squads.team[s];
    const float radiusSq = kContactRadius * kContactRadius;

    uint32_t frontRankCount = 0;
    uint32_t engagedCount   = 0;

    // Ascending member order. members is already ordered by slotIndex within a
    // squad (rebuildSquadMembers guarantees it), so this walk is stable.
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t i = members[start + k];
        if (soldiers.state[i] == SoldierState::Dead) continue;
        if (rankOfSlot(shape, soldiers.slotIndex[i], n) != 0u) continue;

        frontRankCount++;

        const float px = soldiers.posX[i];
        const float py = soldiers.posY[i];
        hash.queryNeighbors(px, py, kContactRadius, scratch);

        for (uint32_t e : scratch) {
            if ((size_t)e == (size_t)i) continue;
            if (soldiers.team[e] == ownTeam) continue;
            if (soldiers.state[e] == SoldierState::Dead) continue;
            const float dx = soldiers.posX[e] - px;
            const float dy = soldiers.posY[e] - py;
            if (dx * dx + dy * dy <= radiusSq) {
                engagedCount++;
                break;   // this member counts once, however many enemies it faces
            }
        }
    }

    // A squad whose entire front rank is dead has no front to fight with. The
    // survivors inherit slot 0 on the next rebuildSquadMembers, so this
    // resolves itself in one tick rather than needing a special case.
    bool inContactNow = false;
    if (frontRankCount > 0) {
        const float engagedFraction = (float)engagedCount / (float)frontRankCount;
        inContactNow = (engagedFraction >= kContactFraction);
    }

    if (inContactNow) {
        squads.contactTimer[s] = kContactClearSeconds;
        squads.contact[s] = 1;
    } else if (squads.contact[s]) {
        // Latched. Bleed the grace period down; clear only when it runs out.
        squads.contactTimer[s] -= dt;
        if (squads.contactTimer[s] <= 0.0f) {
            squads.contactTimer[s] = 0.0f;
            squads.contact[s] = 0;
        }
    }
}
```

Add `src/Contact.cpp` to the `tactix_sim` source list in `c++/CMakeLists.txt`.

Wire it into `c++/src/Simulation.cpp`'s `phaseSquadAggregate`, which must now take `dt`. Change the signature in `Simulation.hpp` to `void phaseSquadAggregate(float dt)`, pass `dt` from `tick()`, and inside the job body:

```cpp
        jobSystem.submit([this, start, end, dt]() {
            // Neighbour buffer reused across every squad in this chunk, so
            // contact detection does not allocate per squad per tick.
            std::vector<uint32_t> scratch;
            scratch.reserve(64);
            for (size_t s = start; s < end; ++s) {
                updateSquadAggregate(soldiers, squads, squadMembers, s);
                detectContact(soldiers, squads, squadMembers, spatialHash, s, dt, scratch);
                selectTargetSoldier(soldiers, squads, squadMembers, s);
            }
        });
```

Add `#include "Contact.hpp"` to `Simulation.cpp`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all five new cases PASS. `test_counters.cpp` still passes: `contact` is not yet in the digest and nothing reads it yet, so behavior is unchanged. If the digest moved, something reads contact prematurely; find it before continuing.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Contact.hpp c++/src/Contact.cpp c++/src/Units.hpp \
        c++/src/Squads.hpp c++/src/Simulation.hpp c++/src/Simulation.cpp \
        c++/tests/test_contact.cpp c++/CMakeLists.txt
git commit -m "feat: detect front-rank contact per squad

A squad is in contact when at least kContactFraction of its front rank has
a live enemy within kContactRadius. A grace period keeps it latched so it
does not flicker between engaged and advancing as individual enemies die.

Reads soldier positions and the spatial hash only, never another squad's
centroid: those are being written concurrently in this same phase, which is
the hazard updateSquadAggregate already documents for facing.

Nothing consumes the flag yet, so the digest is unchanged."
```

---

## Task 4: Anchor latching

This is the fix for the spin. Slots are built from the centroid, the centroid is the mean of the members, and the members chase the slots. Latching a fixed anchor at the moment of contact cuts that loop: the mean of the member positions stops determining where the members are told to stand.

The anchor is the formation's origin **always**, not only in contact. When the squad is free it tracks the centroid exactly, which reproduces today's behavior bit for bit. When contact latches, it stops tracking. When contact clears, it eases back rather than snapping, so the formation does not jump.

**Files:**
- Modify: `c++/src/Squads.hpp`, `c++/src/Contact.cpp`, `c++/src/Soldiers.cpp`, `c++/src/Units.hpp`, `c++/src/Simulation.cpp`
- Test: `c++/tests/test_contact.cpp`

**Interfaces:**
- Consumes: `squads.contact` (Task 3).
- Produces: `SquadHot::anchorX`, `anchorY`. `slotWorldPosition` reads them instead of `centroidX/Y`. Task 6 layers compression on top of the same function; Task 11 reads `contact` for the `Engaged` order.

- [ ] **Step 1: Add the fields and constant**

In `c++/src/Squads.hpp`, add to `SquadHot` and initialize in `spawn()`:

```cpp
    // Formation origin (design 5.2). This, NOT the centroid, is what
    // slotWorldPosition builds slots from. While the squad is free it tracks
    // the centroid exactly, reproducing the pre-contact behaviour bit for bit.
    // On the rising edge of contact it latches, which is what cuts the
    // centroid/slot feedback loop that made melee a rotating blob.
    std::vector<float> anchorX, anchorY;
```

```cpp
        anchorX.push_back(0.0f);
        anchorY.push_back(0.0f);
```

In `c++/src/Units.hpp`, beside the other contact constants:

```cpp
// How long the formation anchor takes to ease back onto the live centroid
// after contact clears. Snapping instead would teleport the whole formation
// by however far the centroid drifted during the fight.
constexpr float kAnchorReleaseSeconds = 0.5f;
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_contact.cpp`:

```cpp
TEST_CASE("a free squad's anchor tracks its centroid exactly") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 600.0f, 100.0f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 313.0f;
    f.squads.centroidY[0] = 207.0f;

    std::vector<uint32_t> scratch;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, 1.0f / 60.0f, scratch);

    REQUIRE(f.squads.contact[0] == 0);
    CHECK(f.squads.anchorX[0] == doctest::Approx(313.0f));
    CHECK(f.squads.anchorY[0] == doctest::Approx(207.0f));
}

TEST_CASE("the anchor latches on the rising edge of contact and then holds") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 150.0f;
    f.squads.centroidY[0] = 100.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);
    CHECK(f.squads.anchorX[0] == doctest::Approx(150.0f));

    // The centroid now drifts, as it would while men shuffle in a melee. The
    // anchor must NOT follow it: that is the whole point.
    f.squads.centroidX[0] = 400.0f;
    f.squads.centroidY[0] = 400.0f;
    for (int t = 0; t < 10; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }
    CHECK(f.squads.contact[0] == 1);
    CHECK(f.squads.anchorX[0] == doctest::Approx(150.0f));
    CHECK(f.squads.anchorY[0] == doctest::Approx(100.0f));
}

TEST_CASE("the anchor eases back to the centroid after contact clears") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 8, kSlotSpacing);
    f.addSquad(Team::B, 100.0f, 100.0f + kContactRadius * 0.5f, 8, kSlotSpacing);
    f.rehash();
    f.squads.centroidX[0] = 100.0f;
    f.squads.centroidY[0] = 100.0f;

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    REQUIRE(f.squads.contact[0] == 1);

    for (uint32_t k = 0; k < f.squads.memberCount[1]; ++k) {
        f.soldiers.state[f.members[f.squads.memberStart[1] + k]] = SoldierState::Dead;
    }
    f.squads.centroidX[0] = 200.0f;   // 100px away from the latched anchor

    // Run out the contact grace period, then the release period.
    const int ticks = (int)((kContactClearSeconds + kAnchorReleaseSeconds * 4.0f) / dt);
    for (int t = 0; t < ticks; ++t) {
        detectContact(f.soldiers, f.squads, f.members, f.hash, 0, dt, scratch);
    }

    CHECK(f.squads.contact[0] == 0);
    // Eased, not snapped: it must have closed most of the gap but the point is
    // that it arrives smoothly rather than in one frame.
    CHECK(f.squads.anchorX[0] == doctest::Approx(200.0f).epsilon(0.02));
}

TEST_CASE("two advancing squads do not pass through each other") {
    // The regression test for the spin (design 2.1). Before anchor latching,
    // both centroids converge on one point and the formations orbit it.
    Simulation sim(1280, 720, 42u);
    sim.init(400);
    sim.setPaused(false);

    const float startGap = std::abs(sim.teamCentroidX(Team::A) - sim.teamCentroidX(Team::B));
    const bool aStartsLeft = sim.teamCentroidX(Team::A) < sim.teamCentroidX(Team::B);
    REQUIRE(startGap > 100.0f);

    for (int t = 0; t < 1200; ++t) sim.tick(1.0f / 60.0f);

    // Whichever side started on the left must still be on the left. Passing
    // through would flip the sign; orbiting a shared point would collapse the
    // gap to near zero.
    const bool aStillLeft = sim.teamCentroidX(Team::A) < sim.teamCentroidX(Team::B);
    CHECK(aStillLeft == aStartsLeft);
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: the three anchor cases FAIL (`anchorX` stays 0, since nothing writes it). The pass-through case may pass or fail depending on the seed; it is the regression guard, and Step 4 is what makes it reliable.

- [ ] **Step 4: Write the implementation**

Add an anchor block to `detectContact` in `c++/src/Contact.cpp`. Insert it at the top, before the early return, so an emptied squad still keeps a sane anchor:

```cpp
    // --- Anchor maintenance (design 5.2) ---
    // Three states, and the ordering matters. `wasInContact` is captured
    // BEFORE the flag is recomputed below, so the rising and falling edges are
    // both detectable from one pass.
    const uint8_t wasInContact = squads.contact[s];
```

Then, at the very end of the function, after `contact` has been updated:

```cpp
    // Free: the anchor IS the centroid. This is what makes an unengaged squad
    // behave exactly as it did before anchoring existed.
    // Latching: on the rising edge, freeze where we stand.
    // Engaged: hold, whatever the centroid does. Cutting this link is the fix
    //          for the centroid/slot feedback loop (design 2.1).
    // Releasing: ease back, so the formation does not teleport by however far
    //          the centroid drifted during the fight.
    if (squads.contact[s]) {
        if (!wasInContact) {
            squads.anchorX[s] = squads.centroidX[s];
            squads.anchorY[s] = squads.centroidY[s];
        }
        // else: hold the latched anchor.
    } else if (wasInContact) {
        // Falling edge this very tick: start easing from where we were.
        squads.anchorX[s] += (squads.centroidX[s] - squads.anchorX[s])
                           * (dt / kAnchorReleaseSeconds);
        squads.anchorY[s] += (squads.centroidY[s] - squads.anchorY[s])
                           * (dt / kAnchorReleaseSeconds);
    } else {
        // An exponential ease, not a snap, so the tick after release is not a
        // discontinuity either. It converges to the centroid within a few
        // frames and then tracks it to within float epsilon, which is what the
        // free-squad case needs.
        const float k = dt / kAnchorReleaseSeconds;
        const float dx = squads.centroidX[s] - squads.anchorX[s];
        const float dy = squads.centroidY[s] - squads.anchorY[s];
        if (dx * dx + dy * dy < 0.01f) {
            squads.anchorX[s] = squads.centroidX[s];
            squads.anchorY[s] = squads.centroidY[s];
        } else {
            squads.anchorX[s] += dx * k;
            squads.anchorY[s] += dy * k;
        }
    }
```

**Note on the free case:** the test "a free squad's anchor tracks its centroid exactly" requires exact tracking. The `dx*dx + dy*dy < 0.01f` snap is what delivers it: once within 0.1px the anchor assigns the centroid outright rather than approaching it asymptotically forever.

The anchor must also be seeded at deployment, or every squad spends its first half-second easing in from the origin. In `c++/src/Simulation.cpp`, in `init()`, immediately after the `rebuildSquadMembers` call at the end:

```cpp
    // Seed each squad's formation anchor from its deployed centroid. Without
    // this every anchor starts at (0,0) and every squad spends its first
    // kAnchorReleaseSeconds dragging its formation in from the world origin.
    for (size_t s = 0; s < squads.count; ++s) {
        updateSquadAggregate(soldiers, squads, squadMembers, s);
        squads.anchorX[s] = squads.centroidX[s];
        squads.anchorY[s] = squads.centroidY[s];
    }
```

Now make `slotWorldPosition` read the anchor. In `c++/src/Soldiers.cpp`, replace the two `squads.centroidX[s]` / `squads.centroidY[s]` reads in the return statement, and suppress the advance lead while engaged:

```cpp
    // Engaged squads do not lead: a formation that has met the enemy is
    // holding ground, not marching. Suppressing the lead here rather than in
    // the caller keeps "where a slot is" answerable from this one function.
    const float lead = (squads.order[s] == (uint8_t)SquadOrder::Advance
                        && !squads.contact[s])
                     ? kAdvanceLead : 0.0f;

    const float leadX = (lead != 0.0f) ? squads.moveX[s] : 0.0f;
    const float leadY = (lead != 0.0f) ? squads.moveY[s] : 0.0f;

    // Built from the ANCHOR, not the centroid (design 5.2). While the squad is
    // free the two are equal, so this is behaviour-preserving; while it is
    // engaged the anchor is frozen, which is what stops the formation chasing
    // its own drifting mean.
    return Vec2{
        squads.anchorX[s] + local.x * rightX + local.y * fx + lead * leadX,
        squads.anchorY[s] + local.x * rightY + local.y * fy + lead * leadY
    };
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all four new cases PASS. `test_counters.cpp` FAILS on `stateDigest`: formations now halt at contact instead of interpenetrating, which changes the whole battle. Expected, and fixed in Step 6.

Also expect `test_steering.cpp` and `test_advance.cpp` to need review. Anchoring is behavior-preserving for a free squad, so they should pass unchanged. **If either fails, do not adjust the test until you have confirmed the cause is the deliberate contact halt and not a regression in free-squad steering.**

- [ ] **Step 6: Regenerate baselines and commit**

Regenerate both baseline files exactly as in Task 2 Step 5, with this note:

```
# Regenerated 2026-08-22: formations now latch a fixed anchor at contact
# instead of building slots from a live centroid that the members themselves
# move. Squads halt on contact rather than interpenetrating, which changes
# every subsequent tick of the battle. Deliberate behavioural change.
```

```bash
git add c++/src/Squads.hpp c++/src/Contact.cpp c++/src/Soldiers.cpp \
        c++/src/Units.hpp c++/src/Simulation.cpp \
        c++/tests/test_contact.cpp c++/tests/baseline/
git commit -m "fix: latch a formation anchor at contact to stop the melee spin

Slots were built from the centroid, the centroid was the mean of the
members, and the members chased the slots. Nothing broke that loop at
contact, so two squads walked their centroids together and both formations
orbited the shared point.

slotWorldPosition now builds from an anchor that tracks the centroid while
the squad is free and freezes on the rising edge of contact. A free squad
behaves exactly as before; an engaged one holds the ground it met the enemy
on. The anchor eases back on release so the formation never teleports.

Advance lead is suppressed while engaged: a formation in contact is holding
ground, not marching."
```

---

## Task 5: Non-penetration

Nothing currently stops two soldiers occupying the same point. The separation force is deliberately weak (10px at strength 300, sized so it never fights a held formation) and must stay that way. Hard collision is a separate mechanism.

**The determinism design is the interesting part.** `phaseMovement` writes integrated positions into scratch arrays; `phaseContact` then reads that snapshot as read-only shared data and each soldier writes only its own position, displacing itself by **half** of each overlap. The neighbor's own job applies the other half. Every agent's displacement is a function of read-only data plus its own index, so it is identical regardless of how work is chunked.

**Files:**
- Modify: `c++/src/Contact.hpp`, `c++/src/Contact.cpp`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`
- Test: `c++/tests/test_contact.cpp`

**Interfaces:**
- Consumes: `kSoldierRadius` (`Units.hpp`), `SpatialHash::queryNeighbors`.
- Produces: `void resolveOverlap(SoldierHot&, const std::vector<float>& nextX, const std::vector<float>& nextY, const SpatialHash&, size_t soldierIndex, std::vector<uint32_t>& scratch)`. Writes `soldiers.posX/posY` for that index only.
- Adds to `Simulation`: `std::vector<float> nextPosX, nextPosY;` and `void phaseContact();` plus `void phaseContactChunk(size_t start, size_t end);`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_contact.cpp`:

```cpp
namespace {
// Runs one resolveOverlap pass over every soldier, exactly as phaseContact
// does: snapshot first, then each soldier displaces only itself from that
// read-only snapshot.
void resolveAll(Fixture& f) {
    std::vector<float> nextX(f.soldiers.posX);
    std::vector<float> nextY(f.soldiers.posY);
    f.rehash();
    std::vector<uint32_t> scratch;
    for (size_t i = 0; i < f.soldiers.count; ++i) {
        resolveOverlap(f.soldiers, nextX, nextY, f.hash, i, scratch);
    }
}
} // namespace

TEST_CASE("a single overlapping pair is separated to exactly touching") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 2, 4.0f);   // 4px apart, radius 4 each
    resolveAll(f);

    const float dx = f.soldiers.posX[1] - f.soldiers.posX[0];
    const float dy = f.soldiers.posY[1] - f.soldiers.posY[0];
    const float d = std::sqrt(dx * dx + dy * dy);

    // Overlap was 8 - 4 = 4px. Each moved half of it, so they end up exactly
    // 2 * kSoldierRadius apart in one pass.
    CHECK(d == doctest::Approx(2.0f * kSoldierRadius).epsilon(1e-4));
}

TEST_CASE("non-overlapping soldiers are not moved at all") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 2, kSlotSpacing);   // 12px apart
    const float x0 = f.soldiers.posX[0];
    const float x1 = f.soldiers.posX[1];
    resolveAll(f);
    CHECK(f.soldiers.posX[0] == doctest::Approx(x0));
    CHECK(f.soldiers.posX[1] == doctest::Approx(x1));
}

TEST_CASE("exactly coincident soldiers separate deterministically") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 2, 0.0f);   // same point exactly
    resolveAll(f);

    const float dx = f.soldiers.posX[1] - f.soldiers.posX[0];
    const float dy = f.soldiers.posY[1] - f.soldiers.posY[0];
    CHECK(std::sqrt(dx * dx + dy * dy) > 0.0f);

    // Repeating from the same input must give the same output: the tie is
    // broken on index, not on iteration order.
    Fixture g;
    g.addSquad(Team::A, 100.0f, 100.0f, 2, 0.0f);
    resolveAll(g);
    CHECK(g.soldiers.posX[0] == doctest::Approx(f.soldiers.posX[0]));
    CHECK(g.soldiers.posX[1] == doctest::Approx(f.soldiers.posX[1]));
}

TEST_CASE("a dead soldier neither pushes nor is pushed") {
    Fixture f;
    f.addSquad(Team::A, 100.0f, 100.0f, 2, 4.0f);
    f.soldiers.state[1] = SoldierState::Dead;
    const float x0 = f.soldiers.posX[0];
    resolveAll(f);
    CHECK(f.soldiers.posX[0] == doctest::Approx(x0));
}

TEST_CASE("a full battle never leaves soldiers more than half overlapped") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);
    for (int t = 0; t < 600; ++t) sim.tick(1.0f / 60.0f);

    // One pass per tick does not solve the constraint to convergence, which is
    // deliberate: a press of bodies should look like a press. So this asserts
    // the weak bound, that nobody is ever more than half inside anybody else.
    // The exact-resolution claim is carried by the pair test above.
    const float floorDist = kSoldierRadius;
    size_t worstPairs = 0;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        for (size_t j = i + 1; j < sim.getAgentCount(); ++j) {
            const float dx = sim.soldierX(j) - sim.soldierX(i);
            const float dy = sim.soldierY(j) - sim.soldierY(i);
            if (dx * dx + dy * dy < floorDist * floorDist) worstPairs++;
        }
    }
    CHECK(worstPairs == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `'resolveOverlap': identifier not found`.

- [ ] **Step 3: Write `resolveOverlap`**

Declare in `c++/src/Contact.hpp`:

```cpp
// Displaces soldier `i` out of any overlap with its neighbours (design 5.3).
//
// Reads positions from the caller's `nextX`/`nextY` snapshot, NOT from
// soldiers.posX/posY, and writes only soldiers.posX[i]/posY[i]. That split is
// what makes this parallel-safe: every agent's displacement is a function of
// read-only shared data and its own index, so the result is identical at any
// worker count and any chunking.
//
// Applies HALF of each overlap. The neighbour's own call applies the other
// half, so a pair separates symmetrically without either side writing the
// other. One pass per tick, deliberately not solved to convergence: an
// instantly-resolved constraint reads as a rigid body, a gradually-resolved
// one reads as a press of bodies.
void resolveOverlap(SoldierHot& soldiers,
                    const std::vector<float>& nextX,
                    const std::vector<float>& nextY,
                    const SpatialHash& hash, size_t soldierIndex,
                    std::vector<uint32_t>& scratch);
```

Implement in `c++/src/Contact.cpp`:

```cpp
void resolveOverlap(SoldierHot& soldiers,
                    const std::vector<float>& nextX,
                    const std::vector<float>& nextY,
                    const SpatialHash& hash, size_t i,
                    std::vector<uint32_t>& scratch) {
    const float px = nextX[i];
    const float py = nextY[i];

    if (soldiers.state[i] == SoldierState::Dead) {
        soldiers.posX[i] = px;
        soldiers.posY[i] = py;
        return;
    }

    // 2 * kSoldierRadius is 8px. That sits below kSeparationRadius (10px),
    // which sits below kSlotSpacing (12px), so a soldier standing correctly on
    // its slot feels zero force from any of the three. Same layering argument
    // kSeparationRadius's own comment makes, extended by one term: it is why
    // adding hard collision does not fight held formations.
    const float minDist = 2.0f * kSoldierRadius;
    const float minDistSq = minDist * minDist;

    // The hash was built from posX/posY at the top of the tick, so it is one
    // movement step stale here. At maxSpeed (150 px/s) and 60 Hz that is 2.5px
    // against 50px cells, so a 3x3 query still finds everyone within 8px. A
    // documented tolerance, and it saves a full rebuild.
    hash.queryNeighbors(px, py, minDist, scratch);

    float dx = 0.0f;
    float dy = 0.0f;

    for (uint32_t n : scratch) {
        if ((size_t)n == i) continue;
        if (soldiers.state[n] == SoldierState::Dead) continue;

        const float ox = px - nextX[n];
        const float oy = py - nextY[n];
        const float dSq = ox * ox + oy * oy;
        if (dSq >= minDistSq) continue;

        if (dSq < 1e-6f) {
            // Exactly coincident, so there is no separating axis to use. Break
            // the tie on index: i pushes +x when its neighbour's index is
            // higher, and that neighbour's own call sees a LOWER index and
            // pushes -x. Opposite by construction, and identical on every
            // thread and platform.
            dx += (n > (uint32_t)i ? 1.0f : -1.0f) * kSoldierRadius;
            continue;
        }

        const float d = std::sqrt(dSq);
        const float push = (minDist - d) * 0.5f;
        dx += (ox / d) * push;
        dy += (oy / d) * push;
    }

    soldiers.posX[i] = px + dx;
    soldiers.posY[i] = py + dy;
}
```

- [ ] **Step 4: Wire the phase in**

In `c++/src/Simulation.hpp`, add the scratch arrays beside `prevPosX/prevPosY`:

```cpp
    // Integrated positions, written by phase 8 and consumed by phase 9. The
    // split exists so non-penetration reads a consistent read-only snapshot
    // rather than positions other jobs are concurrently updating.
    std::vector<float> nextPosX;
    std::vector<float> nextPosY;
```

and declare the phase beside `phaseMovement`:

```cpp
    void phaseContact();                                     // Phase 9: non-penetration
    void phaseContactChunk(size_t start, size_t end);        // Parallel version
```

In `c++/src/Simulation.cpp`, size the arrays wherever `prevPosX`/`prevPosY` are sized (in `init()`, alongside their `push_back`, and in any `reserve`). The simplest correct edit is immediately after the deployment loop in `init()`:

```cpp
    // Sized once, alongside prevPos. compactDead keeps them in step.
    nextPosX.assign(soldiers.count, 0.0f);
    nextPosY.assign(soldiers.count, 0.0f);
```

`compactDead` shrinks `prevPosX`/`prevPosY` and must shrink these too, or the arrays desync. Rather than widening `compactDead`'s signature for two more arrays, resize them at the top of `phaseContact`, which runs after compaction every tick:

```cpp
void Simulation::phaseContact() {
    const size_t chunkSize = 256;
    for (size_t start = 0; start < soldiers.count; start += chunkSize) {
        const size_t end = std::min(start + chunkSize, soldiers.count);
        jobSystem.submit([this, start, end]() {
            phaseContactChunk(start, end);
        });
        workCounters.add(workCounters.jobsDispatched, 1);
    }
    // Barrier owned by tick(), not this function.
}

void Simulation::phaseContactChunk(size_t start, size_t end) {
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(64);
    for (size_t i = start; i < end; ++i) {
        resolveOverlap(soldiers, nextPosX, nextPosY, spatialHash, i, localNeighbors);
    }
}
```

In `phaseMovementChunk`, change the two position writes from `soldiers.posX[i] = newX;` / `soldiers.posY[i] = newY;` to:

```cpp
        // Written to the snapshot, not to posX/posY. Phase 9 reads this
        // snapshot to resolve overlap and is what finally writes position.
        nextPosX[i] = newX;
        nextPosY[i] = newY;
```

Every other read of `soldiers.posX[i]` inside `phaseMovementChunk` (the building and tree deflection maths) stays as it is: those read the soldier's own pre-move position, which is correct.

In `tick()`, size the snapshot before movement and call the new phase after it:

```cpp
    // Phase 8: parallel over soldiers. Writes nextPos, not pos.
    nextPosX.resize(soldiers.count);
    nextPosY.resize(soldiers.count);
    phaseMovement(dt);
    jobSystem.waitAll();

    // Phase 9: parallel over soldiers. Reads the nextPos snapshot read-only
    // and writes each soldier's own final position. The barrier above is what
    // makes that snapshot read-only, so it is load-bearing, not decoration.
    phaseContact();
    jobSystem.waitAll();

    clampToWorld();
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: the five new cases PASS. The thread-invariance case in `test_determinism.cpp` must still PASS; if it does not, the snapshot split is wrong somewhere and no baseline should be regenerated until it is understood. `test_counters.cpp` FAILS on `stateDigest` and on `candidatesExamined`/`cellsVisited` (a new query per soldier per tick), both expected.

- [ ] **Step 6: Regenerate baselines and commit**

Regenerate both baselines as in Task 2 Step 5, with this note:

```
# Regenerated 2026-08-22: soldiers now collide with each other. phaseContact
# adds one neighbour query per soldier per tick, so candidatesExamined and
# cellsVisited rise, and bodies no longer overlap, so the digest moves.
# Both are the intended cost and the intended behaviour.
```

```bash
git add c++/src/Contact.hpp c++/src/Contact.cpp c++/src/Simulation.hpp \
        c++/src/Simulation.cpp c++/tests/test_contact.cpp c++/tests/baseline/
git commit -m "feat: add soldier non-penetration as a post-movement phase

Nothing stopped two soldiers occupying the same point. Separation is
deliberately weak, sized so it never fights a held formation, so hard
collision has to be a separate mechanism.

phaseMovement now writes a position snapshot and phaseContact reads it
read-only, each soldier displacing only itself by half of each overlap.
The neighbour applies the other half, so pairs separate symmetrically with
no cross-agent writes and the result is identical at any worker count.

8px non-penetration sits below the 10px separation radius, which sits below
the 12px slot spacing, so a correctly-slotted soldier feels none of them."
```

---

## Task 6: Rank cohesion

Rank depth compresses by how organized the squad is, so a shaken unit visibly bunches while a disciplined one keeps its spacing. Lateral spread is never scaled: a formation that narrowed under pressure would look like a funnel, not a crowd.

Compression is applied to `(raw.y - mean.y)`, the mean-centered offset, **not** to `raw.y`. Scaling the centered value keeps the mean of the slot offsets at exactly zero, which is what preserves the centroid fixed-point property `formationMeanOffset` exists to establish. Scaling the raw value would break it and reintroduce a slow backward drift.

`morale` and `discipline` are both still constant at 1.0 until Task 7, so this task is behavior-preserving by construction and the digest must not move.

**Files:**
- Modify: `c++/src/Units.hpp`, `c++/src/Soldiers.hpp`, `c++/src/Soldiers.cpp`
- Test: `c++/tests/test_formation.cpp`

**Interfaces:**
- Consumes: `squads.discipline`, `squads.morale`, `squads.contact` (Task 3).
- Produces: `float squadCompression(const SquadHot& squads, size_t squadIndex)`, declared in `Soldiers.hpp`. Task 8 makes its inputs actually vary.

- [ ] **Step 1: Add the constants**

In `c++/src/Units.hpp`:

```cpp
// Rank-depth compression (design 5.4). Cohesion is discipline * morale, and
// it scales how deep a formation stands: an organized unit keeps full rank
// spacing, a shaken one collapses toward its front rank. Only DEPTH is
// scaled, never width, because a formation that narrowed under pressure would
// read as a funnel rather than as a crowd.
constexpr float kMinCompression = 0.45f;

// Additional squeeze while engaged. Men press forward into a fight. Ranks stay
// ranks: compression scales every rank's depth uniformly and never reorders
// slots, so rank order is preserved at any value.
constexpr float kContactCompression = 0.8f;
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_formation.cpp` (add `#include "Soldiers.hpp"` and `#include "Squads.hpp"`):

```cpp
TEST_CASE("a fully cohesive squad is not compressed at all") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 1.0f;
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;
    CHECK(squadCompression(q, 0) == doctest::Approx(1.0f));
}

TEST_CASE("a broken squad compresses to the floor, never past it") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 0.0f;
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;
    CHECK(squadCompression(q, 0) == doctest::Approx(kMinCompression));
    CHECK(squadCompression(q, 0) > 0.0f);
}

TEST_CASE("compression is monotone in cohesion") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.contact[0] = 0;

    float previous = -1.0f;
    for (float m : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        q.morale[0] = m;
        const float c = squadCompression(q, 0);
        CHECK(c > previous);
        previous = c;
    }
}

TEST_CASE("an engaged squad presses tighter than a free one at equal morale") {
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.morale[0] = 1.0f;
    q.discipline[0] = 1.0f;

    q.contact[0] = 0;
    const float free = squadCompression(q, 0);
    q.contact[0] = 1;
    const float engaged = squadCompression(q, 0);
    CHECK(engaged < free);
}

TEST_CASE("compression preserves rank ORDER, only rank spacing") {
    // The failure this guards against is a shaken squad collapsing into a
    // point, which would make rear ranks fight and break the front-rank-only
    // property entirely.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.contact[0] = 1;
    q.morale[0] = 0.0f;               // worst case: floor times contact squeeze
    q.facingX[0] = 0.0f;
    q.facingY[0] = 1.0f;
    q.anchorX[0] = 0.0f;
    q.anchorY[0] = 0.0f;
    q.memberCount[0] = 40;
    q.order[0] = (uint8_t)SquadOrder::Hold;

    // Walk one column of the formation and confirm each successive rank is
    // strictly behind the one in front, along facing.
    const uint32_t width = detail::rankWidth(40, 2.0f);
    float previousForward = 1e30f;
    for (uint32_t rank = 0; rank * width < 40; ++rank) {
        const uint16_t slot = (uint16_t)(rank * width);
        const Vec2 p = slotWorldPosition(q, 0, slot, 40);
        const float forward = p.x * q.facingX[0] + p.y * q.facingY[0];
        CHECK(forward < previousForward);
        previousForward = forward;
    }
}

TEST_CASE("compression keeps the mean slot offset at the anchor") {
    // formationMeanOffset exists so the centroid is a genuine fixed point.
    // Scaling the RAW forward offset instead of the mean-centered one would
    // break that and make the formation drift backward every tick.
    SquadHot q;
    q.spawn(Team::A, UnitType::Infantry);
    q.discipline[0] = 1.0f;
    q.morale[0] = 0.3f;               // some arbitrary partial compression
    q.contact[0] = 0;
    q.facingX[0] = 1.0f;
    q.facingY[0] = 0.0f;
    q.anchorX[0] = 500.0f;
    q.anchorY[0] = 300.0f;
    q.memberCount[0] = 37;
    q.order[0] = (uint8_t)SquadOrder::Hold;

    float sumX = 0.0f, sumY = 0.0f;
    for (uint16_t k = 0; k < 37; ++k) {
        const Vec2 p = slotWorldPosition(q, 0, k, 37);
        sumX += p.x;
        sumY += p.y;
    }
    CHECK(sumX / 37.0f == doctest::Approx(500.0f).epsilon(1e-4));
    CHECK(sumY / 37.0f == doctest::Approx(300.0f).epsilon(1e-4));
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `'squadCompression': identifier not found`.

- [ ] **Step 4: Write the implementation**

Declare in `c++/src/Soldiers.hpp`:

```cpp
// How tightly this squad stands, 0..1, applied to formation DEPTH only.
// cohesion = discipline * morale, mapped onto [kMinCompression, 1], times an
// extra squeeze while engaged. Lives here rather than in Formation.hpp because
// it reads squad state, and formationSlot is deliberately a pure function of
// shape and index with no squad knowledge at all.
float squadCompression(const SquadHot& squads, size_t squadIndex);
```

Implement in `c++/src/Soldiers.cpp`:

```cpp
float squadCompression(const SquadHot& squads, size_t s) {
    // Both inputs are clamped 0..1 by Morale.cpp, but clamp defensively here
    // too: this feeds slot positions, and a value outside the range would
    // either invert the formation or fling it apart.
    const float m = (squads.morale[s] < 0.0f) ? 0.0f
                  : (squads.morale[s] > 1.0f) ? 1.0f : squads.morale[s];
    const float d = (squads.discipline[s] < 0.0f) ? 0.0f
                  : (squads.discipline[s] > 1.0f) ? 1.0f : squads.discipline[s];

    const float cohesion = d * m;
    float c = kMinCompression + (1.0f - kMinCompression) * cohesion;
    if (squads.contact[s]) c *= kContactCompression;
    return c;
}
```

In `slotWorldPosition`, apply it to the mean-centered forward offset:

```cpp
    const Vec2 mean = formationMeanOffset(shape, memberCount);
    // Compression scales the MEAN-CENTERED depth, not the raw depth. Scaling
    // the raw value would move the mean of the slot offsets off zero, and
    // formationMeanOffset exists precisely to keep it there: without that, the
    // squad chases its own receding centroid backward every tick.
    const float compression = squadCompression(squads, s);
    const Vec2 local{ raw.x - mean.x, (raw.y - mean.y) * compression };
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all six new cases PASS.

**The digest must NOT move.** `morale` and `discipline` are both still 1.0 everywhere and `contact` only matters once morale varies, so `squadCompression` returns 1.0 for every free squad and this is behavior-preserving except for engaged squads. If `test_counters.cpp` fails only on `stateDigest`, that is the `kContactCompression` term on engaged squads and is expected. If it fails on anything else, stop and find out why before regenerating.

- [ ] **Step 6: Regenerate baselines and commit**

Regenerate both baselines as in Task 2 Step 5, with this note:

```
# Regenerated 2026-08-22: engaged squads now compress their rank depth by
# kContactCompression, so rear ranks stand closer to the front while
# fighting. Morale and discipline are still constant, so this is the only
# behavioural change in this commit.
```

```bash
git add c++/src/Units.hpp c++/src/Soldiers.hpp c++/src/Soldiers.cpp \
        c++/tests/test_formation.cpp c++/tests/baseline/
git commit -m "feat: compress rank depth by squad cohesion

Formation depth now scales with discipline times morale, so a shaken unit
collapses toward its front rank while a disciplined one holds its spacing.
Width is never scaled: a formation that narrowed under pressure would read
as a funnel rather than a crowd.

Applied to the mean-centered offset, not the raw one, so the mean of the
slot offsets stays at zero and the anchor remains a genuine fixed point.

Morale and discipline are still constant at 1.0, so only the extra squeeze
on engaged squads changes behaviour today."
```

---

## Task 7: Morale and rout as pure functions

`casualties` and `officerDied` are populated every tick by `recordCasualties` and read by nothing. This is the consumer they were recorded for.

Both functions are written and tested standalone here. Task 8 wires them in. Splitting it that way means the formula can be checked against hand-computed values without a whole simulation in the way.

**Files:**
- Create: `c++/src/Morale.hpp`, `c++/src/Morale.cpp`, `c++/tests/test_morale.cpp`
- Modify: `c++/src/Units.hpp`, `c++/src/Squads.hpp`, `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `SquadHot::morale`, `discipline`, `memberCount`, and the `casualties`/`officerDied` vectors `Simulation` already owns.
- Produces:
  - `void updateMorale(SquadHot& squads, const std::vector<uint32_t>& casualties, const std::vector<uint8_t>& officerDied, float dt)`
  - `void applyRoutTransitions(SquadHot& squads, float dt)`
  - `float disciplineForUnit(UnitType u)`
- Adds to `SquadHot`: `std::vector<uint8_t> rearThreat`, `std::vector<float> nearestEnemyDist`, `std::vector<float> rallyTimer`. Task 8 fills the first two; this task only reads them.

- [ ] **Step 1: Add the fields and constants**

In `c++/src/Squads.hpp`, add to `SquadHot` and initialize in `spawn()`:

```cpp
    // Morale inputs, written in phase 4 by the squad decide (which may read
    // every squad's centroid, because phase 2's barrier has made them
    // read-only) and consumed in serial resolution by Morale.cpp.
    //
    // Both are computed inside the enemy loop selectTargetSquad ALREADY walks,
    // so they cost nothing asymptotically. Computing them in resolution
    // instead would be O(squads squared) on the serial path every tick.
    std::vector<uint8_t> rearThreat;        // an enemy squad sits behind us
    std::vector<float>   nearestEnemyDist;  // to the closest live enemy squad

    // Counts UP the time spent clear of enemies while routing. Rally needs
    // sustained safety, not an instant of it.
    std::vector<float>   rallyTimer;
```

```cpp
        rearThreat.push_back(0);
        nearestEnemyDist.push_back(1e30f);
        rallyTimer.push_back(0.0f);
```

In `c++/src/Units.hpp`:

```cpp
// Morale (design 6). All tuning knobs; the existence of each input is not.
//
// Morale falls from casualties taken this tick as a fraction of squad size,
// from the officer dying, and from an enemy in the rear arc. It recovers on a
// base rate. Discipline scales BOTH resistance to loss and recovery rate,
// which is what makes a disciplined unit meaningfully different rather than
// just slower to break.
constexpr float kMoraleLossPerCasualtyFraction = 2.0f;
constexpr float kMoraleOfficerDeathPenalty     = 0.15f;
constexpr float kMoraleRearThreatPerSecond     = 0.12f;
constexpr float kMoraleRecoveryPerSecond       = 0.05f;

// Rout thresholds. routThreshold scales DOWN with discipline, so a
// disciplined squad holds at a morale a levy would have broken at.
// kRallyThreshold sits above the worst-case rout threshold: the gap is
// hysteresis, and without it a squad at the boundary oscillates every tick.
constexpr float kBaseRoutThreshold = 0.30f;
constexpr float kRallyThreshold    = 0.45f;
constexpr float kRallyRadius       = 220.0f;
constexpr float kRallyDuration     = 3.0f;    // seconds clear of enemies

// Discipline by unit type. Cavalry are the least steady, archers are fragile
// but not undisciplined, infantry are the anchor. Constant per type for now:
// per-squad variation is a tuning knob nobody has asked for yet.
constexpr float kDisciplineInfantry = 0.85f;
constexpr float kDisciplineArcher   = 0.60f;
constexpr float kDisciplineCavalry  = 0.70f;
```

- [ ] **Step 2: Write the failing test**

Create `c++/tests/test_morale.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Morale.hpp"
#include "Squads.hpp"
#include "Units.hpp"
#include <vector>

namespace {
// One squad, fully fresh, with the given size and discipline.
SquadHot oneSquad(uint32_t members, float discipline, UnitType u = UnitType::Infantry) {
    SquadHot q;
    q.spawn(Team::A, u);
    q.memberCount[0] = members;
    q.morale[0] = 1.0f;
    q.discipline[0] = discipline;
    return q;
}
} // namespace

TEST_CASE("an untouched squad recovers toward full morale and stops at 1") {
    SquadHot q = oneSquad(20, kDisciplineInfantry);
    q.morale[0] = 0.5f;
    const std::vector<uint32_t> none(1, 0u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(q, none, noOfficer, 1.0f);
    CHECK(q.morale[0] > 0.5f);

    for (int t = 0; t < 100; ++t) updateMorale(q, none, noOfficer, 1.0f);
    CHECK(q.morale[0] == doctest::Approx(1.0f));
}

TEST_CASE("casualties drop morale in proportion to the fraction lost") {
    SquadHot small = oneSquad(10, 1.0f);
    SquadHot large = oneSquad(100, 1.0f);
    const std::vector<uint32_t> twoDead(1, 2u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(small, twoDead, noOfficer, 1.0f / 60.0f);
    updateMorale(large, twoDead, noOfficer, 1.0f / 60.0f);

    // Two dead out of ten hurts far more than two out of a hundred.
    CHECK(small.morale[0] < large.morale[0]);
}

TEST_CASE("losing the officer costs morale on top of the casualty itself") {
    SquadHot withOfficer = oneSquad(20, 1.0f);
    SquadHot without     = oneSquad(20, 1.0f);
    const std::vector<uint32_t> oneDead(1, 1u);

    updateMorale(withOfficer, oneDead, std::vector<uint8_t>(1, (uint8_t)1), 1.0f / 60.0f);
    updateMorale(without,     oneDead, std::vector<uint8_t>(1, (uint8_t)0), 1.0f / 60.0f);

    CHECK(withOfficer.morale[0] < without.morale[0]);
}

TEST_CASE("discipline blunts losses and speeds recovery") {
    SquadHot steady = oneSquad(20, 0.95f);
    SquadHot levy   = oneSquad(20, 0.20f);
    const std::vector<uint32_t> fourDead(1, 4u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    updateMorale(steady, fourDead, noOfficer, 1.0f / 60.0f);
    updateMorale(levy,   fourDead, noOfficer, 1.0f / 60.0f);
    CHECK(steady.morale[0] > levy.morale[0]);
}

TEST_CASE("an enemy in the rear arc erodes morale over time") {
    SquadHot flanked = oneSquad(20, 1.0f);
    SquadHot safe    = oneSquad(20, 1.0f);
    flanked.morale[0] = 0.5f;
    safe.morale[0]    = 0.5f;
    flanked.rearThreat[0] = 1;
    const std::vector<uint32_t> none(1, 0u);
    const std::vector<uint8_t> noOfficer(1, (uint8_t)0);

    for (int t = 0; t < 60; ++t) {
        updateMorale(flanked, none, noOfficer, 1.0f / 60.0f);
        updateMorale(safe,    none, noOfficer, 1.0f / 60.0f);
    }
    CHECK(flanked.morale[0] < safe.morale[0]);
}

TEST_CASE("morale is clamped to 0..1 under absurd input") {
    SquadHot q = oneSquad(4, 0.0f);
    const std::vector<uint32_t> wipe(1, 400u);
    updateMorale(q, wipe, std::vector<uint8_t>(1, (uint8_t)1), 1.0f);
    CHECK(q.morale[0] >= 0.0f);
    CHECK(q.morale[0] <= 1.0f);
}

TEST_CASE("an empty squad's morale is left alone rather than divided by zero") {
    SquadHot q = oneSquad(0, 1.0f);
    q.morale[0] = 0.7f;
    updateMorale(q, std::vector<uint32_t>(1, 0u),
                 std::vector<uint8_t>(1, (uint8_t)0), 1.0f / 60.0f);
    CHECK(q.morale[0] == doctest::Approx(0.7f));
}

TEST_CASE("a squad below its rout threshold breaks") {
    SquadHot q = oneSquad(20, 0.0f);       // no discipline: threshold is the base
    q.morale[0] = kBaseRoutThreshold - 0.01f;
    applyRoutTransitions(q, 1.0f / 60.0f);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);
}

TEST_CASE("a disciplined squad holds where an undisciplined one breaks") {
    SquadHot steady = oneSquad(20, 1.0f);
    SquadHot levy   = oneSquad(20, 0.0f);
    const float m = kBaseRoutThreshold - 0.01f;
    steady.morale[0] = m;
    levy.morale[0]   = m;

    applyRoutTransitions(steady, 1.0f / 60.0f);
    applyRoutTransitions(levy,   1.0f / 60.0f);

    CHECK(steady.order[0] != (uint8_t)SquadOrder::Rout);
    CHECK(levy.order[0]   == (uint8_t)SquadOrder::Rout);
}

TEST_CASE("a routing squad rallies only after sustained safety") {
    SquadHot q = oneSquad(20, 0.0f);
    q.order[0] = (uint8_t)SquadOrder::Rout;
    q.morale[0] = kRallyThreshold + 0.05f;
    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;    // clear of enemies

    const float dt = 1.0f / 60.0f;
    const int needed = (int)(kRallyDuration / dt);
    for (int t = 0; t < needed - 2; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);   // not yet

    for (int t = 0; t < 4; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Hold);
}

TEST_CASE("an enemy nearby resets the rally timer") {
    SquadHot q = oneSquad(20, 0.0f);
    q.order[0] = (uint8_t)SquadOrder::Rout;
    q.morale[0] = kRallyThreshold + 0.05f;

    const float dt = 1.0f / 60.0f;
    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;
    for (int t = 0; t < (int)(kRallyDuration / dt) - 10; ++t) applyRoutTransitions(q, dt);

    q.nearestEnemyDist[0] = kRallyRadius * 0.5f;      // an enemy closes in
    applyRoutTransitions(q, dt);
    CHECK(q.rallyTimer[0] == doctest::Approx(0.0f));

    q.nearestEnemyDist[0] = kRallyRadius * 2.0f;
    for (int t = 0; t < 20; ++t) applyRoutTransitions(q, dt);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);   // had to start over
}

TEST_CASE("rout clears contact so a broken formation stops holding a line") {
    SquadHot q = oneSquad(20, 0.0f);
    q.morale[0] = 0.0f;
    q.contact[0] = 1;
    applyRoutTransitions(q, 1.0f / 60.0f);
    CHECK(q.order[0] == (uint8_t)SquadOrder::Rout);
    CHECK(q.contact[0] == 0);
}

TEST_CASE("disciplineForUnit gives every type a distinct steadiness") {
    CHECK(disciplineForUnit(UnitType::Infantry) == doctest::Approx(kDisciplineInfantry));
    CHECK(disciplineForUnit(UnitType::Archer)   == doctest::Approx(kDisciplineArcher));
    CHECK(disciplineForUnit(UnitType::Cavalry)  == doctest::Approx(kDisciplineCavalry));
}
```

Add `tests/test_morale.cpp` to the `tactix_tests` source list.

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, cannot open source file `Morale.hpp`.

- [ ] **Step 4: Write the implementation**

Create `c++/src/Morale.hpp`:

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SquadHot;

// Steadiness of a unit type, 0..1. Scales both resistance to morale loss and
// recovery rate, so a disciplined unit is genuinely different in kind rather
// than merely slower to break.
float disciplineForUnit(UnitType u);

// Resolution step 5 (design 6). Updates every squad's morale from the
// casualties recorded this tick, the officer-death flag, and the rear-arc
// threat flag phase 4 wrote.
//
// Serial: this runs inside phaseResolution, the only place cross-agent state
// is touched. It reads `casualties` and `officerDied` positionally by squad
// index, so both MUST be sized to squads.count by the caller.
void updateMorale(SquadHot& squads,
                  const std::vector<uint32_t>& casualties,
                  const std::vector<uint8_t>& officerDied,
                  float dt);

// Rout entry and exit (design 6). Applied here in resolution rather than in
// the squad scorer, so a breaking squad does not wait for its next decide.
//
// Entry is immediate on crossing the threshold. Exit needs BOTH a morale
// recovery past kRallyThreshold and kRallyDuration seconds clear of enemies,
// tracked in rallyTimer. Without an explicit exit, Rout is terminal and a
// routed squad runs off the map.
void applyRoutTransitions(SquadHot& squads, float dt);
```

Create `c++/src/Morale.cpp`:

```cpp
#include "Morale.hpp"
#include "Squads.hpp"

namespace {
float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace

float disciplineForUnit(UnitType u) {
    switch (u) {
        case UnitType::Archer:  return kDisciplineArcher;
        case UnitType::Cavalry: return kDisciplineCavalry;
        default:                return kDisciplineInfantry;
    }
}

void updateMorale(SquadHot& squads,
                  const std::vector<uint32_t>& casualties,
                  const std::vector<uint8_t>& officerDied,
                  float dt) {
    for (size_t s = 0; s < squads.count; ++s) {
        // A wiped-out squad keeps its last morale rather than being driven to
        // a meaningless value. Squads are never destroyed, so an emptied one
        // stays in the array forever and must not poison anything reading it.
        if (squads.memberCount[s] == 0) continue;

        const uint32_t dead = (s < casualties.size()) ? casualties[s] : 0u;
        const uint8_t lostOfficer = (s < officerDied.size()) ? officerDied[s] : (uint8_t)0;

        // Size BEFORE this tick's losses: memberCount was rebuilt from the
        // survivors, so the dead are no longer in it. Losing 2 of 10 has to
        // read as a fifth of the squad, not as a quarter of what is left.
        const float sizeBefore = (float)squads.memberCount[s] + (float)dead;
        const float lossFraction = (sizeBefore > 0.0f) ? (float)dead / sizeBefore : 0.0f;

        float drop = lossFraction * kMoraleLossPerCasualtyFraction;
        if (lostOfficer) drop += kMoraleOfficerDeathPenalty;
        if (squads.rearThreat[s]) drop += kMoraleRearThreatPerSecond * dt;

        // Discipline blunts the whole loss, not just part of it. At discipline
        // 1.0 a squad takes half the morale damage of a discipline-0 rabble.
        drop *= (1.0f - squads.discipline[s] * 0.5f);

        const float recovery = kMoraleRecoveryPerSecond * dt
                             * (0.5f + squads.discipline[s] * 0.5f);

        squads.morale[s] = clamp01(squads.morale[s] - drop + recovery);
    }
}

void applyRoutTransitions(SquadHot& squads, float dt) {
    for (size_t s = 0; s < squads.count; ++s) {
        const bool routing = (squads.order[s] == (uint8_t)SquadOrder::Rout);

        if (!routing) {
            // A disciplined squad holds at a morale a levy would break at.
            const float threshold = kBaseRoutThreshold
                                  * (1.0f - squads.discipline[s] * 0.5f);
            if (squads.morale[s] < threshold && squads.memberCount[s] > 0) {
                squads.order[s] = (uint8_t)SquadOrder::Rout;
                squads.rallyTimer[s] = 0.0f;
                // A formation that has broken is not holding a line. Releasing
                // the anchor lets the squad actually run rather than orbiting
                // the point it was latched to.
                squads.contact[s] = 0;
                squads.contactTimer[s] = 0.0f;
            }
            continue;
        }

        // Routing. Rally needs sustained safety, not an instant of it, so any
        // enemy inside kRallyRadius resets the clock rather than pausing it.
        if (squads.nearestEnemyDist[s] > kRallyRadius) {
            squads.rallyTimer[s] += dt;
        } else {
            squads.rallyTimer[s] = 0.0f;
        }

        if (squads.morale[s] >= kRallyThreshold &&
            squads.rallyTimer[s] >= kRallyDuration) {
            // Hold, not Advance: a squad that has just rallied re-enters
            // normal scoring on its next decide rather than charging straight
            // back into whatever broke it.
            squads.order[s] = (uint8_t)SquadOrder::Hold;
            squads.rallyTimer[s] = 0.0f;
        }
    }
}
```

Add `src/Morale.cpp` to the `tactix_sim` source list.

**`SquadOrder::Rout` does not exist yet.** Add the full set to `c++/src/Units.hpp` now, appended so existing values keep their numbers:

```cpp
// Appended, never renumbered: order feeds the state digest, so changing an
// existing value silently invalidates every committed baseline.
//
// Engaged is the contact halt (design 5.2). Withdraw and Rout differ
// deliberately: a withdrawing squad keeps its formation and rallies on
// command, a routing one does neither.
enum class SquadOrder : uint8_t {
    Hold = 0, Advance = 1, Engaged = 2, Flank = 3,
    Screen = 4, Withdraw = 5, Rout = 6
};
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all thirteen new cases PASS. `test_counters.cpp` still PASSES: nothing calls these functions yet, so the digest is unchanged. Verify that, because a moved digest here means something is calling them prematurely.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Morale.hpp c++/src/Morale.cpp c++/src/Units.hpp \
        c++/src/Squads.hpp c++/tests/test_morale.cpp c++/CMakeLists.txt
git commit -m "feat: add morale and rout transitions as pure functions

casualties and officerDied have been recorded every tick since plan 2 and
read by nothing. This is the consumer they were recorded for.

Morale falls from the fraction of the squad lost, the officer dying, and an
enemy in the rear arc, and recovers on a base rate. Discipline scales both
resistance and recovery, so a steady unit differs in kind rather than just
in speed. Rout entry is immediate; exit needs both a morale recovery and
sustained distance from the enemy, the gap between the two thresholds being
the hysteresis that stops a squad oscillating at the boundary.

Not wired in yet, so the digest is unchanged."
```

---

## Task 8: Wire morale in and compute its threat inputs

Two halves. The threat inputs (`rearThreat`, `nearestEnemyDist`) are computed inside the enemy loop `selectTargetSquad` **already walks**, so they cost nothing asymptotically. The morale update goes into resolution as step 5, the slot the existing comment reserves for it.

This is the task that makes `squadCompression` (Task 6) start varying, so shaken squads begin visibly bunching.

**Files:**
- Modify: `c++/src/Squads.cpp`, `c++/src/Simulation.cpp`
- Test: `c++/tests/test_morale.cpp`

**Interfaces:**
- Consumes: `updateMorale`, `applyRoutTransitions`, `disciplineForUnit` (Task 7).
- Produces: populated `squads.rearThreat` and `squads.nearestEnemyDist` every tick, and live `squads.morale`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_morale.cpp` (add `#include "Simulation.hpp"`):

```cpp
TEST_CASE("discipline is seeded per unit type at deployment") {
    Simulation sim(1280, 720, 42u);
    sim.init(600);
    bool sawArcher = false, sawInfantry = false;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        const float d = sim.squadDiscipline(s);
        CHECK(d > 0.0f);
        CHECK(d <= 1.0f);
        if (sim.squadUnitType(s) == UnitType::Archer)   { sawArcher = true;   }
        if (sim.squadUnitType(s) == UnitType::Infantry) { sawInfantry = true; }
    }
    REQUIRE(sawArcher);
    REQUIRE(sawInfantry);
}

TEST_CASE("morale actually falls once a battle starts costing lives") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    for (int t = 0; t < 200; ++t) sim.tick(1.0f / 60.0f);
    // Nobody has died this early, so morale should be pinned at full.
    float minEarly = 1.0f;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        minEarly = std::min(minEarly, sim.squadMorale(s));
    }
    CHECK(minEarly == doctest::Approx(1.0f));

    for (int t = 0; t < 1500; ++t) sim.tick(1.0f / 60.0f);
    float minLate = 1.0f;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        minLate = std::min(minLate, sim.squadMorale(s));
    }
    CHECK(minLate < 1.0f);
}

TEST_CASE("nearestEnemyDist is populated and finite once squads have decided") {
    Simulation sim(1280, 720, 42u);
    sim.init(600);
    sim.setPaused(false);
    for (int t = 0; t < 10; ++t) sim.tick(1.0f / 60.0f);

    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        CHECK(sim.squadNearestEnemyDist(s) < 1e29f);
        CHECK(sim.squadNearestEnemyDist(s) >= 0.0f);
    }
}
```

These need three read-only accessors. Add them to `c++/src/Simulation.hpp` beside the existing squad accessors:

```cpp
    float    squadMorale(size_t s) const { return squads.morale[s]; }
    float    squadDiscipline(size_t s) const { return squads.discipline[s]; }
    float    squadNearestEnemyDist(size_t s) const { return squads.nearestEnemyDist[s]; }
    UnitType squadUnitType(size_t s) const { return squads.unitType[s]; }
    uint8_t  squadOrder(size_t s) const { return squads.order[s]; }
    uint8_t  squadContact(size_t s) const { return squads.contact[s]; }
```

`squadOrder` and `squadContact` are used by later tasks' tests; adding all six now avoids touching this header six more times.

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: `discipline is seeded per unit type` FAILS (discipline is 1.0 for everything), and `morale actually falls` FAILS (morale never moves).

- [ ] **Step 3: Seed discipline at deployment**

In `c++/src/Simulation.cpp`'s `init()`, where each squad is spawned, set its discipline. Find the `squads.spawn(...)` call and follow it with:

```cpp
            // Steadiness is a property of the unit type (Morale.cpp). Set at
            // deployment rather than defaulted in SquadHot::spawn, because
            // spawn does not know what it is spawning until the caller says.
            squads.discipline[squadId] = disciplineForUnit(unit);
```

Add `#include "Morale.hpp"` to `Simulation.cpp`.

- [ ] **Step 4: Compute the threat inputs in the existing enemy loop**

In `c++/src/Squads.cpp`, inside `selectTargetSquad`, the loop over enemy squads already computes `dx`, `dy`, and `d` for every enemy. Extend it rather than adding a second loop:

```cpp
    float bestDistSq = 1e30f;
    uint16_t best = squads.targetSquad[s];
    bool found = false;

    // Rear-arc threat and nearest-enemy distance are computed HERE, inside the
    // loop that already visits every enemy squad, so they are free. Morale
    // (serial resolution) consumes both. Doing it there instead would put an
    // O(squads squared) walk on the serial path every tick.
    float nearestSq = 1e30f;
    uint8_t rear = 0;
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];

    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;

        if (d < nearestSq) nearestSq = d;

        // Behind us and close enough to matter. A dot product against facing
        // is the whole rear-arc test: negative means the enemy is on the side
        // we are not looking at.
        if (d < kRallyRadius * kRallyRadius && (dx * fx + dy * fy) < 0.0f) {
            rear = 1;
        }

        if (d < bestDistSq) {
            bestDistSq = d;
            best = (uint16_t)e;
            found = true;
        }
    }

    squads.nearestEnemyDist[s] = (nearestSq < 1e30f) ? std::sqrt(nearestSq) : 1e30f;
    squads.rearThreat[s] = rear;
```

This reads other squads' centroids, which is safe in phase 4 and only in phase 4: phase 2's barrier has made every centroid read-only for the rest of the tick. The existing comment in `selectTargetSquad` already makes this argument for facing.

- [ ] **Step 5: Wire the update into resolution**

In `c++/src/Simulation.cpp`'s `phaseResolution`, replace the placeholder comment for step 5:

```cpp
    recordCasualties(soldiers, casualties, officerDied);    // step 4
    // Step 5. Both run BEFORE compaction, because casualties and officerDied
    // are indexed by squad and describe what happened this tick; running them
    // after would be equally correct but would separate them from the data
    // they read. applyRoutTransitions runs second because it consumes the
    // morale updateMorale just wrote.
    updateMorale(squads, casualties, officerDied, kFixedTimestep);
    applyRoutTransitions(squads, kFixedTimestep);
```

`phaseResolution` has no `dt`. Rather than threading it through, define the timestep as a constant in `c++/src/Units.hpp`, which is honest about the simulation being fixed-step:

```cpp
// The simulation is fixed-step by design (see the determinism contract): tick
// is always called with this value, and the benchmark and tests all use it.
// Named here so resolution-phase code that has no dt parameter can still
// express rates per second rather than per tick.
constexpr float kFixedTimestep = 1.0f / 60.0f;
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: the three new cases PASS. `test_counters.cpp` FAILS on `stateDigest`: morale and discipline now vary, which drives `squadCompression`, which moves every rear-rank slot. Expected.

- [ ] **Step 7: Regenerate baselines and commit**

Regenerate both baselines as in Task 2 Step 5, with this note:

```
# Regenerated 2026-08-22: morale and discipline are live. Discipline is
# seeded per unit type at deployment, morale falls with casualties and
# officer deaths, and both feed rank compression, so formation depth now
# varies with how badly a squad has been hurt. Squads can also rout.
```

```bash
git add c++/src/Squads.cpp c++/src/Simulation.hpp c++/src/Simulation.cpp \
        c++/src/Units.hpp c++/tests/test_morale.cpp c++/tests/baseline/
git commit -m "feat: wire morale into resolution and compute its threat inputs

Discipline is seeded per unit type at deployment. Morale updates in
resolution as step 5, the slot reserved for it, and rout transitions follow
so a breaking squad does not wait for its next decide.

rearThreat and nearestEnemyDist are computed inside the enemy loop
selectTargetSquad already walks, so they cost nothing asymptotically.
Computing them in resolution would have put an O(squads squared) walk on
the serial path every tick.

This is what makes rank compression start varying: shaken squads now
visibly bunch."
```

---

## Task 9: The army tier and its front line

Two entities, one per team. The front line is the load-bearing field: it makes "behind our line" a well-defined place, which every archer positioning decision needs and which no squad can compute on its own.

**Files:**
- Create: `c++/src/Army.hpp`, `c++/src/Army.cpp`, `c++/tests/test_army.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `SquadHot` centroids, team, unit type, member counts.
- Produces:
  - `struct ArmyHot` with `spawn()`.
  - `enum class SquadRole : uint8_t { Reserve = 0, Line = 1, Screen = 2, Flank = 3, Shoot = 4 }`
  - `enum class ArmyPosture : uint8_t { Press = 0, Hold = 1, Fallback = 2 }`
  - `float squadStrength(const SquadHot& squads, size_t squadIndex)`
  - `void updateArmyAggregate(const SquadHot& squads, ArmyHot& armies)`
- Task 10 consumes all of these; Task 11 reads `frontX/Y` and `frontDirX/Y` for role anchors.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_army.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Army.hpp"
#include "Squads.hpp"
#include "Units.hpp"
#include <cmath>

namespace {
// Two armies with squads placed by hand, so aggregates can be checked against
// values computed on paper rather than against whatever the sim happens to do.
struct ArmyFixture {
    SquadHot squads;
    ArmyHot armies;

    ArmyFixture() { armies.spawn(); armies.spawn(); }

    uint16_t add(Team t, UnitType u, float cx, float cy, uint32_t members) {
        const uint16_t s = (uint16_t)squads.count;
        squads.spawn(t, u);
        squads.centroidX[s] = cx;
        squads.centroidY[s] = cy;
        squads.memberCount[s] = members;
        return s;
    }
};
} // namespace

TEST_CASE("army strength sums its live squads by unit type") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 100.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 140.0f, 100.0f, 10);
    f.add(Team::A, UnitType::Archer,   100.0f,  60.0f, 12);
    f.add(Team::B, UnitType::Cavalry,  600.0f, 100.0f,  8);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.strengthInfantry[0] == doctest::Approx(30.0f));
    CHECK(f.armies.strengthArcher[0]   == doctest::Approx(12.0f));
    CHECK(f.armies.strengthCavalry[0]  == doctest::Approx(0.0f));
    CHECK(f.armies.strengthCavalry[1]  == doctest::Approx(8.0f));
}

TEST_CASE("a wiped-out squad contributes nothing") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 100.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 140.0f, 100.0f, 0);   // annihilated
    updateArmyAggregate(f.squads, f.armies);
    CHECK(f.armies.strengthInfantry[0] == doctest::Approx(20.0f));
}

TEST_CASE("the front line sits on the infantry, not on the whole army") {
    // Archers stand well behind the line. If the front were the mean of every
    // squad it would be dragged backward and 'behind our line' would stop
    // meaning anything useful.
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 200.0f, 140.0f, 20);
    f.add(Team::A, UnitType::Archer,    50.0f, 120.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 120.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontX[0] == doctest::Approx(200.0f));
    CHECK(f.armies.frontY[0] == doctest::Approx(120.0f));
}

TEST_CASE("the front direction points at the enemy army and is unit length") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontDirX[0] == doctest::Approx(1.0f));
    CHECK(f.armies.frontDirY[0] == doctest::Approx(0.0f));
    CHECK(f.armies.frontDirX[1] == doctest::Approx(-1.0f));

    const float len = std::sqrt(f.armies.frontDirX[0] * f.armies.frontDirX[0]
                              + f.armies.frontDirY[0] * f.armies.frontDirY[0]);
    CHECK(len == doctest::Approx(1.0f));
}

TEST_CASE("an army with no infantry falls back to its whole-army centroid") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer, 300.0f, 200.0f, 10);
    f.add(Team::B, UnitType::Infantry, 800.0f, 200.0f, 10);
    updateArmyAggregate(f.squads, f.armies);
    CHECK(f.armies.frontX[0] == doctest::Approx(300.0f));
}

TEST_CASE("an annihilated army produces no NaN") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 0);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);

    CHECK(f.armies.frontX[0] == f.armies.frontX[0]);            // not NaN
    const float len = std::sqrt(f.armies.frontDirX[0] * f.armies.frontDirX[0]
                              + f.armies.frontDirY[0] * f.armies.frontDirY[0]);
    CHECK(len == doctest::Approx(1.0f));
}

TEST_CASE("squadStrength weights members by unit type") {
    ArmyFixture f;
    const uint16_t inf = f.add(Team::A, UnitType::Infantry, 0.0f, 0.0f, 10);
    const uint16_t cav = f.add(Team::A, UnitType::Cavalry,  0.0f, 0.0f, 10);
    // Cavalry hit harder per man, so an equal head count is not equal strength.
    CHECK(squadStrength(f.squads, cav) > squadStrength(f.squads, inf));
}
```

Add `tests/test_army.cpp` to the `tactix_tests` source list.

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, cannot open source file `Army.hpp`.

- [ ] **Step 3: Write the implementation**

Add the strength weights to `c++/src/Units.hpp`:

```cpp
// Combat weight per man, used by the army tier to size how much force an
// enemy squad demands. Cavalry hit hardest per head, archers least in a
// stand-up fight, so an equal head count is not an equal threat.
constexpr float kStrengthPerMan[kUnitTypeCount] = {
    /* Infantry */ 1.0f,
    /* Archer   */ 0.7f,
    /* Cavalry  */ 1.6f,
};
```

Create `c++/src/Army.hpp`:

```cpp
#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SquadHot;

// What the commander has told a squad to be. Appended, never renumbered:
// role reaches the state digest.
enum class SquadRole : uint8_t {
    Reserve = 0,   // hold behind the line, fill gaps
    Line    = 1,   // anchor the battle line, engage the assigned enemy
    Screen  = 2,   // stand between wardSquad and its nearest threat
    Flank   = 3,   // wide route to an assigned enemy's flank
    Shoot   = 4,   // hold a firing position behind the line
};

enum class ArmyPosture : uint8_t { Press = 0, Hold = 1, Fallback = 2 };

// The army tier: exactly two entries, one per team (design 7.1). Two entities
// make a serial decide phase free, and serial makes bit-reproducibility free.
struct ArmyHot {
    std::vector<float> strengthInfantry, strengthArcher, strengthCavalry;
    std::vector<float> centroidX, centroidY;

    // The battle line. frontX/Y is where this army's infantry stands;
    // frontDirX/Y points at the enemy army. Together they define "behind our
    // line", which is what archer positioning and reserve placement both need
    // and what no individual squad can work out on its own.
    std::vector<float> frontX, frontY, frontDirX, frontDirY;

    std::vector<uint8_t> posture;

    size_t count = 0;

    void spawn() {
        strengthInfantry.push_back(0.0f);
        strengthArcher.push_back(0.0f);
        strengthCavalry.push_back(0.0f);
        centroidX.push_back(0.0f);
        centroidY.push_back(0.0f);
        frontX.push_back(0.0f);
        frontY.push_back(0.0f);
        frontDirX.push_back(1.0f);
        frontDirY.push_back(0.0f);
        posture.push_back((uint8_t)ArmyPosture::Press);
        count++;
    }
};

// Combat weight of a squad: live members times their type's weight.
float squadStrength(const SquadHot& squads, size_t squadIndex);

// Recomputes both armies' aggregates and front lines from the squad tier.
// Serial, and called once per tick before any role assignment.
//
// Walks squads in ascending index order and accumulates on one thread, so the
// sums are bit-reproducible regardless of worker count.
void updateArmyAggregate(const SquadHot& squads, ArmyHot& armies);
```

Create `c++/src/Army.cpp`:

```cpp
#include "Army.hpp"
#include "Squads.hpp"
#include <cmath>

float squadStrength(const SquadHot& squads, size_t s) {
    return (float)squads.memberCount[s] * kStrengthPerMan[(int)squads.unitType[s]];
}

void updateArmyAggregate(const SquadHot& squads, ArmyHot& armies) {
    for (size_t a = 0; a < armies.count; ++a) {
        armies.strengthInfantry[a] = 0.0f;
        armies.strengthArcher[a]   = 0.0f;
        armies.strengthCavalry[a]  = 0.0f;
    }

    // Accumulated in ascending squad order on one thread, so the result does
    // not depend on how anything upstream was chunked.
    float sumX[2]      = { 0.0f, 0.0f };
    float sumY[2]      = { 0.0f, 0.0f };
    float weight[2]    = { 0.0f, 0.0f };
    float infSumX[2]   = { 0.0f, 0.0f };
    float infSumY[2]   = { 0.0f, 0.0f };
    float infWeight[2] = { 0.0f, 0.0f };

    for (size_t s = 0; s < squads.count; ++s) {
        if (squads.memberCount[s] == 0) continue;
        const size_t a = (size_t)squads.team[s];
        if (a >= armies.count) continue;

        const float men = (float)squads.memberCount[s];
        switch (squads.unitType[s]) {
            case UnitType::Archer:  armies.strengthArcher[a]   += men; break;
            case UnitType::Cavalry: armies.strengthCavalry[a]  += men; break;
            default:                armies.strengthInfantry[a] += men; break;
        }

        sumX[a] += squads.centroidX[s] * men;
        sumY[a] += squads.centroidY[s] * men;
        weight[a] += men;

        // The front is the INFANTRY mean, not the whole-army mean. Archers
        // stand well back, and letting them drag the front line rearward would
        // make "behind our line" a place that is already behind the archers.
        if (squads.unitType[s] == UnitType::Infantry) {
            infSumX[a] += squads.centroidX[s] * men;
            infSumY[a] += squads.centroidY[s] * men;
            infWeight[a] += men;
        }
    }

    for (size_t a = 0; a < armies.count; ++a) {
        if (weight[a] > 0.0f) {
            armies.centroidX[a] = sumX[a] / weight[a];
            armies.centroidY[a] = sumY[a] / weight[a];
        }
        // An annihilated army keeps its last centroid rather than going to
        // the world origin or to NaN. Armies are never destroyed, so anything
        // still reading this must see a sane value.

        if (infWeight[a] > 0.0f) {
            armies.frontX[a] = infSumX[a] / infWeight[a];
            armies.frontY[a] = infSumY[a] / infWeight[a];
        } else {
            // No infantry left: an army of archers and horse has no shield
            // wall, so its "front" is simply where it is.
            armies.frontX[a] = armies.centroidX[a];
            armies.frontY[a] = armies.centroidY[a];
        }
    }

    // Front direction, computed after every front is final so each army can
    // read the other's.
    for (size_t a = 0; a < armies.count; ++a) {
        const size_t other = (a == 0) ? 1u : 0u;
        if (other >= armies.count) continue;
        const float dx = armies.centroidX[other] - armies.centroidX[a];
        const float dy = armies.centroidY[other] - armies.centroidY[a];
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-6f) {
            armies.frontDirX[a] = dx / len;
            armies.frontDirY[a] = dy / len;
        }
        // else: keep the previous direction, which spawn() seeded to (1,0), so
        // this is never zero length and never NaN.
    }
}
```

Add `src/Army.cpp` to the `tactix_sim` source list.

- [ ] **Step 4: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all seven new cases PASS. Nothing is wired into `Simulation` yet, so the digest must be unchanged.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Army.hpp c++/src/Army.cpp c++/src/Units.hpp \
        c++/tests/test_army.cpp c++/CMakeLists.txt
git commit -m "feat: add the army tier with aggregates and a front line

Two entities, one per team, so a serial decide phase is free and
bit-reproducibility comes with it.

The front line is derived from infantry centroids rather than from the
whole army: archers stand well back, and letting them drag the front
rearward would make 'behind our line' a place that is already behind the
archers. That phrase has to mean something, because archer positioning and
reserve placement are both defined in terms of it.

Not wired into Simulation yet, so the digest is unchanged."
```

---

## Task 10: Role and target assignment

This is the direct answer to "squads do not coordinate". Today every squad picks the nearest enemy independently, so a single forward enemy squad attracts the whole army while the rest of the enemy line advances unopposed.

Everything here iterates in ascending squad index and breaks ties on the lower index. That is what makes the result identical on every platform and every thread count.

**Files:**
- Modify: `c++/src/Army.hpp`, `c++/src/Army.cpp`, `c++/src/Squads.hpp`
- Test: `c++/tests/test_army.cpp`

**Interfaces:**
- Consumes: `squadStrength`, `updateArmyAggregate` (Task 9).
- Produces: `void assignRoles(SquadHot& squads, const ArmyHot& armies, Team team)`. Writes `squads.role`, `squads.wardSquad`, and `squads.targetSquad` for that team's squads only.
- Adds to `SquadHot`: `std::vector<uint8_t> role`, `std::vector<uint16_t> wardSquad`.

- [ ] **Step 1: Add the fields and constants**

In `c++/src/Squads.hpp`, add to `SquadHot` and initialize in `spawn()`:

```cpp
    // Commander assignment (design 7.3). Written only by phaseArmyDecide,
    // read by the squad decide. wardSquad is UINT16_MAX when this squad holds
    // no Screen assignment.
    std::vector<uint8_t>  role;
    std::vector<uint16_t> wardSquad;
```

```cpp
        role.push_back(0);                 // SquadRole::Reserve
        wardSquad.push_back(UINT16_MAX);
```

In `c++/src/Units.hpp`:

```cpp
// How close an enemy melee squad must be before an archer squad is judged to
// need a bodyguard. Deliberately larger than kArcherPanicRadius (design 8.5):
// the screen should already be in place by the time the archers would panic.
constexpr float kScreenThreatRadius = 400.0f;

// How often each army re-decides roles, in ticks. The two armies are offset by
// team so they never decide on the same tick, and an assignment persists long
// enough to be legible rather than churning every frame.
constexpr uint32_t kArmyDecideInterval = 30u;
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_army.cpp`:

```cpp
namespace {
size_t countRole(const SquadHot& q, Team t, SquadRole r) {
    size_t n = 0;
    for (size_t s = 0; s < q.count; ++s) {
        if (q.team[s] == t && q.role[s] == (uint8_t)r) n++;
    }
    return n;
}
} // namespace

TEST_CASE("every archer squad is told to shoot") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Archer,   100.0f, 140.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 120.0f, 20);
    f.add(Team::B, UnitType::Infantry, 900.0f, 120.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Shoot) == 2);
}

TEST_CASE("a threatened archer squad gets exactly one screen") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 260.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Infantry, 320.0f, 100.0f, 20);
    // An enemy well inside kScreenThreatRadius of the archers.
    f.add(Team::B, UnitType::Infantry, 100.0f + kScreenThreatRadius * 0.5f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 1);
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.role[s] == (uint8_t)SquadRole::Screen) {
            CHECK(f.squads.wardSquad[s] == archers);
        }
    }
}

TEST_CASE("an unthreatened archer squad gets no screen") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 100.0f + kScreenThreatRadius * 3.0f, 100.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 0);
}

TEST_CASE("an army with no archers assigns no screens") {
    ArmyFixture f;
    for (int i = 0; i < 4; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    f.add(Team::B, UnitType::Infantry, 300.0f, 160.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Screen) == 0);
}

TEST_CASE("line squads spread across enemies instead of piling on the nearest") {
    // The regression test for the actual complaint. Four equal infantry
    // squads against four equal enemies: each enemy should draw one.
    ArmyFixture f;
    for (int i = 0; i < 4; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    std::vector<uint16_t> enemies;
    for (int i = 0; i < 4; ++i) {
        enemies.push_back(f.add(Team::B, UnitType::Infantry,
                                800.0f, 100.0f + (float)i * 40.0f, 20));
    }
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    std::vector<int> load(f.squads.count, 0);
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] == Team::A && f.squads.role[s] == (uint8_t)SquadRole::Line) {
            load[f.squads.targetSquad[s]]++;
        }
    }
    for (uint16_t e : enemies) {
        CHECK(load[e] == 1);
    }
}

TEST_CASE("a stronger enemy squad draws proportionally more attackers") {
    ArmyFixture f;
    for (int i = 0; i < 6; ++i) {
        f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 40.0f, 20);
    }
    const uint16_t big   = f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 60);
    const uint16_t small = f.add(Team::B, UnitType::Infantry, 800.0f, 300.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    int bigLoad = 0, smallLoad = 0;
    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] != Team::A) continue;
        if (f.squads.role[s] != (uint8_t)SquadRole::Line) continue;
        if (f.squads.targetSquad[s] == big)   bigLoad++;
        if (f.squads.targetSquad[s] == small) smallLoad++;
    }
    CHECK(bigLoad > smallLoad);
}

TEST_CASE("cavalry are sent to flank") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Cavalry,  200.0f, 100.0f, 10);
    f.add(Team::A, UnitType::Infantry, 200.0f, 200.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 200.0f, 20);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    CHECK(countRole(f.squads, Team::A, SquadRole::Flank) == 1);
}

TEST_CASE("every squad ends up with a live enemy target") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Archer,   100.0f, 100.0f, 12);
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::A, UnitType::Cavalry,  200.0f, 200.0f, 10);
    const uint16_t enemy = f.add(Team::B, UnitType::Infantry, 800.0f, 150.0f, 20);
    f.add(Team::B, UnitType::Infantry, 850.0f, 150.0f, 0);   // wiped out
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);

    for (size_t s = 0; s < f.squads.count; ++s) {
        if (f.squads.team[s] != Team::A) continue;
        CHECK(f.squads.targetSquad[s] == enemy);   // the only live one
    }
}

TEST_CASE("assignment leaves an army with no live enemies untouched") {
    ArmyFixture f;
    f.add(Team::A, UnitType::Infantry, 200.0f, 100.0f, 20);
    f.add(Team::B, UnitType::Infantry, 800.0f, 100.0f, 0);
    updateArmyAggregate(f.squads, f.armies);
    assignRoles(f.squads, f.armies, Team::A);   // must not crash or hang
    CHECK(f.squads.count == 2);
}

TEST_CASE("assignment is deterministic from identical input") {
    auto build = []() {
        auto f = std::make_unique<ArmyFixture>();
        for (int i = 0; i < 5; ++i) {
            f->add(Team::A, UnitType::Infantry, 200.0f, 100.0f + (float)i * 37.0f, 18 + i);
        }
        f->add(Team::A, UnitType::Archer,  120.0f, 180.0f, 12);
        f->add(Team::A, UnitType::Cavalry, 210.0f, 300.0f, 10);
        for (int i = 0; i < 4; ++i) {
            f->add(Team::B, UnitType::Infantry, 800.0f, 90.0f + (float)i * 41.0f, 15 + i * 3);
        }
        updateArmyAggregate(f->squads, f->armies);
        assignRoles(f->squads, f->armies, Team::A);
        return f;
    };

    auto a = build();
    auto b = build();
    for (size_t s = 0; s < a->squads.count; ++s) {
        CHECK(a->squads.role[s]        == b->squads.role[s]);
        CHECK(a->squads.targetSquad[s] == b->squads.targetSquad[s]);
        CHECK(a->squads.wardSquad[s]   == b->squads.wardSquad[s]);
    }
}
```

Add `#include <memory>` and `#include <vector>` to the test file.

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `'assignRoles': identifier not found`.

- [ ] **Step 4: Write the implementation**

Declare in `c++/src/Army.hpp`:

```cpp
// Assigns a role, a ward, and a target to every live squad of `team`
// (design 7.4). Writes only that team's squads.
//
// Serial, and deterministic by construction: every loop walks squads in
// ascending index and every tie breaks on the lower index. That is what makes
// the result identical on every platform and at every worker count, which
// matters because role and targetSquad both reach the state digest.
//
// Cost is O(ownSquads x enemySquads), about 40,000 operations at 200 squads a
// side, run once per team per kArmyDecideInterval ticks. Negligible against a
// 10,000-agent tick, and the reason an O(squads squared) assignment is
// affordable where an O(soldiers squared) one would not be.
void assignRoles(SquadHot& squads, const ArmyHot& armies, Team team);
```

Implement in `c++/src/Army.cpp` (add `#include <algorithm>`):

```cpp
namespace {

bool isMelee(UnitType u) { return u != UnitType::Archer; }

float distBetween(const SquadHot& q, size_t a, size_t b) {
    const float dx = q.centroidX[b] - q.centroidX[a];
    const float dy = q.centroidY[b] - q.centroidY[a];
    return std::sqrt(dx * dx + dy * dy);
}

// Nearest live enemy squad to `s`, or UINT16_MAX if the enemy is annihilated.
// Ascending walk with strict less-than, so equidistant enemies resolve to the
// lowest index on every platform.
uint16_t nearestEnemy(const SquadHot& q, size_t s) {
    uint16_t best = UINT16_MAX;
    float bestDist = 1e30f;
    for (size_t e = 0; e < q.count; ++e) {
        if (q.team[e] == q.team[s]) continue;
        if (q.memberCount[e] == 0) continue;
        const float d = distBetween(q, s, e);
        if (d < bestDist) { bestDist = d; best = (uint16_t)e; }
    }
    return best;
}

} // namespace

void assignRoles(SquadHot& squads, const ArmyHot& armies, Team team) {
    (void)armies;   // aggregates are read by the role anchors, not by assignment

    // Live squads of each side, in ascending index order.
    std::vector<uint16_t> own, foe;
    own.reserve(squads.count);
    foe.reserve(squads.count);
    for (size_t s = 0; s < squads.count; ++s) {
        if (squads.memberCount[s] == 0) continue;
        if (squads.team[s] == team) own.push_back((uint16_t)s);
        else                        foe.push_back((uint16_t)s);
    }

    // Nothing left to fight. Leave every role and target as it stands rather
    // than inventing an assignment against a dead enemy.
    if (own.empty() || foe.empty()) return;

    // Step 1: every squad starts unassigned, with the nearest live enemy as a
    // default target. Later steps override the target where they have a better
    // opinion, so no squad can ever come out of this without one.
    for (uint16_t s : own) {
        squads.role[s] = (uint8_t)SquadRole::Reserve;
        squads.wardSquad[s] = UINT16_MAX;
        const uint16_t n = nearestEnemy(squads, s);
        if (n != UINT16_MAX) squads.targetSquad[s] = n;
    }

    // Step 2: archers shoot.
    for (uint16_t s : own) {
        if (squads.unitType[s] == UnitType::Archer) {
            squads.role[s] = (uint8_t)SquadRole::Shoot;
        }
    }

    // Step 3: each threatened archer squad claims at most ONE infantry squad
    // as its screen. The cap is what stops the whole army becoming
    // bodyguards; walking archers in ascending index makes which archer gets
    // the last spare infantry squad deterministic.
    std::vector<uint8_t> claimed(squads.count, 0u);
    for (uint16_t a : own) {
        if (squads.unitType[a] != UnitType::Archer) continue;

        uint16_t threat = UINT16_MAX;
        float threatDist = 1e30f;
        for (uint16_t e : foe) {
            if (!isMelee(squads.unitType[e])) continue;
            const float d = distBetween(squads, a, e);
            if (d < threatDist) { threatDist = d; threat = e; }
        }
        if (threat == UINT16_MAX || threatDist > kScreenThreatRadius) continue;

        uint16_t guard = UINT16_MAX;
        float guardDist = 1e30f;
        for (uint16_t g : own) {
            if (squads.unitType[g] != UnitType::Infantry) continue;
            if (claimed[g]) continue;
            const float d = distBetween(squads, a, g);
            if (d < guardDist) { guardDist = d; guard = g; }
        }
        if (guard == UINT16_MAX) continue;

        claimed[guard] = 1;
        squads.role[guard] = (uint8_t)SquadRole::Screen;
        squads.wardSquad[guard] = a;
        squads.targetSquad[guard] = threat;
    }

    // Step 4: cavalry flank the most exposed enemy, defined as the one whose
    // nearest friendly (enemy-side) support is farthest away. The cheapest
    // definition of an exposed flank that is not simply "nearest".
    for (uint16_t c : own) {
        if (squads.unitType[c] != UnitType::Cavalry) continue;

        uint16_t pick = UINT16_MAX;
        float bestExposure = -1.0f;
        for (uint16_t e : foe) {
            float support = 1e30f;
            for (uint16_t o : foe) {
                if (o == e) continue;
                support = std::min(support, distBetween(squads, e, o));
            }
            // A lone enemy squad has no support at all, so it is maximally
            // exposed. Capped rather than left at 1e30 so the comparison below
            // stays meaningful when several enemies are alone.
            if (support > 1e29f) support = 1e6f;
            if (support > bestExposure) { bestExposure = support; pick = e; }
        }
        if (pick != UINT16_MAX) {
            squads.role[c] = (uint8_t)SquadRole::Flank;
            squads.targetSquad[c] = pick;
        }
    }

    // Step 5: remaining infantry form the line, spread over enemy squads by
    // greedy lowest-load where each enemy's demand is its strength. THIS is
    // the target spreading: without it every squad picks the nearest enemy and
    // one forward enemy squad draws the whole army.
    std::vector<float> load(squads.count, 0.0f);
    std::vector<float> demand(squads.count, 0.0f);
    for (uint16_t e : foe) {
        demand[e] = std::max(squadStrength(squads, e), 1.0f);
    }

    for (uint16_t s : own) {
        if (squads.role[s] != (uint8_t)SquadRole::Reserve) continue;
        if (squads.unitType[s] != UnitType::Infantry) continue;

        const float ourStrength = std::max(squadStrength(squads, s), 1.0f);

        uint16_t pick = UINT16_MAX;
        float bestRatio = 1e30f;
        for (uint16_t e : foe) {
            // Fill the least-covered enemy first. Ties break on the lower
            // enemy index because the walk is ascending and the test is
            // strict less-than.
            const float ratio = (load[e] + ourStrength) / demand[e];
            if (ratio < bestRatio) { bestRatio = ratio; pick = e; }
        }
        if (pick == UINT16_MAX) continue;

        load[pick] += ourStrength;
        squads.role[s] = (uint8_t)SquadRole::Line;
        squads.targetSquad[s] = pick;
    }

    // Step 6: anything still unassigned stays Reserve, with the default
    // nearest-enemy target step 1 gave it.
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/build.bat -t`
Expected: all ten new cases PASS. Nothing calls `assignRoles` from `Simulation` yet, so the digest is unchanged.

- [ ] **Step 6: Commit**

```bash
git add c++/src/Army.hpp c++/src/Army.cpp c++/src/Squads.hpp c++/src/Units.hpp \
        c++/tests/test_army.cpp
git commit -m "feat: assign squad roles and spread targets from the army tier

Every squad picked the nearest enemy independently, so one forward enemy
squad drew the whole army while the rest of its line advanced unopposed.
Line squads are now distributed by greedy lowest-load over enemy squads
weighted by strength, which is the direct fix.

Archers shoot, each threatened archer squad claims at most one infantry
squad as a screen, and cavalry go for the enemy squad whose nearest support
is farthest away.

Every loop walks ascending and every tie breaks on the lower index, so the
assignment is identical on every platform. Not wired in yet."
```

---

## Task 11: Wire the army phase in and rewrite the squad decide around roles

`phaseArmyDecide` slots between squad-aggregate and squad-decide. Its placement is forced: it must run after every squad centroid is final (phase 2's barrier) and before any squad chooses an objective.

`selectTargetSquad` stops choosing targets and becomes `squadDecide`: it turns the commander's role into an order and a role anchor, then hands that anchor to the existing terrain scorer. `chooseTacticalObjective` keeps its mechanism entirely; it just scores candidates around the role's anchor rather than always around the direct lane.

**Files:**
- Modify: `c++/src/Squads.hpp`, `c++/src/Squads.cpp`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/WorkCounters.hpp`, `c++/tests/test_counters.cpp`, `c++/tests/baseline/*.txt`
- Test: `c++/tests/test_army.cpp`

**Interfaces:**
- Consumes: `assignRoles`, `updateArmyAggregate`, `ArmyHot`, `SquadRole` (Tasks 9, 10); `slewFacing` (Task 2); `squads.contact` (Task 3).
- Produces:
  - `Vec2 roleAnchorFor(const SquadHot& squads, const ArmyHot& armies, size_t squadIndex)`
  - `void squadDecide(SquadHot& squads, const ArmyHot& armies, size_t squadIndex, const TerrainField& terrain, float dt)` replaces `selectTargetSquad`. Task 14 adds the archer panic branch to it; Task 15 adds the scorer terms.
  - `chooseTacticalObjective` gains a `Vec2 roleAnchor` parameter before its out-params.
  - `Simulation::phaseArmyDecide()`.
  - `WorkCounters::armyDecisions`.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_army.cpp` (add `#include "Simulation.hpp"`):

```cpp
TEST_CASE("roles are assigned on the very first tick, not on the stagger") {
    // The stagger must not leave a squad acting on a role it never received.
    Simulation sim(1280, 720, 42u);
    sim.init(600);
    sim.setPaused(false);
    sim.tick(1.0f / 60.0f);

    size_t shooters = 0, archers = 0;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (sim.squadUnitType(s) == UnitType::Archer) {
            archers++;
            if (sim.squadRole(s) == (uint8_t)SquadRole::Shoot) shooters++;
        }
    }
    REQUIRE(archers > 0);
    CHECK(shooters == archers);
}

TEST_CASE("an engaged squad is given the Engaged order, not Advance") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    bool sawEngaged = false;
    for (int t = 0; t < 1500 && !sawEngaged; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t s = 0; s < sim.getSquadCount(); ++s) {
            if (sim.squadContact(s)) {
                CHECK(sim.squadOrder(s) != (uint8_t)SquadOrder::Advance);
                sawEngaged = true;
            }
        }
    }
    CHECK(sawEngaged);
}

TEST_CASE("a screening squad puts itself between its ward and the threat") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);
    for (int t = 0; t < 400; ++t) sim.tick(1.0f / 60.0f);

    size_t checked = 0;
    for (size_t s = 0; s < sim.getSquadCount(); ++s) {
        if (sim.squadRole(s) != (uint8_t)SquadRole::Screen) continue;
        const uint16_t ward = sim.squadWardSquad(s);
        const uint16_t threat = sim.squadTargetSquad(s);
        REQUIRE(ward != UINT16_MAX);

        // The objective must be closer to the threat than the ward is: that is
        // what 'between' means operationally.
        const float ox = sim.squadObjectiveX(s), oy = sim.squadObjectiveY(s);
        const float tx = sim.squadCentroidX(threat), ty = sim.squadCentroidY(threat);
        const float wx = sim.squadCentroidX(ward),   wy = sim.squadCentroidY(ward);

        const float objToThreat = std::sqrt((ox - tx) * (ox - tx) + (oy - ty) * (oy - ty));
        const float wardToThreat = std::sqrt((wx - tx) * (wx - tx) + (wy - ty) * (wy - ty));
        CHECK(objToThreat < wardToThreat);
        checked++;
    }
    REQUIRE(checked > 0);
}

TEST_CASE("thread count still does not change simulation state") {
    // The new serial army phase and the role-driven decide must not have
    // introduced any dependence on chunking.
    auto run = [](uint32_t threads) {
        Simulation sim(1280, 720, 42u, threads);
        sim.init(2000);
        sim.setPaused(false);
        for (int t = 0; t < 300; ++t) sim.tick(1.0f / 60.0f);
        return sim.stateDigest();
    };
    const uint64_t single = run(1u);
    CHECK(run(2u)  == single);
    CHECK(run(8u)  == single);
    CHECK(run(15u) == single);
}
```

Add the accessors these need to `c++/src/Simulation.hpp`:

```cpp
    uint8_t  squadRole(size_t s) const { return squads.role[s]; }
    uint16_t squadWardSquad(size_t s) const { return squads.wardSquad[s]; }
    float    squadObjectiveX(size_t s) const { return squads.objectiveX[s]; }
    float    squadObjectiveY(size_t s) const { return squads.objectiveY[s]; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile errors for the missing accessors, then, once those are added, `roles are assigned on the very first tick` FAILS because nothing calls `assignRoles`.

- [ ] **Step 3: Add the counter**

In `c++/src/WorkCounters.hpp`, add the field and reset it:

```cpp
    std::atomic<uint64_t> armyDecisions{0};        // army-tier decisions evaluated
```

```cpp
        armyDecisions.store(0, std::memory_order_relaxed);
```

In `c++/tests/test_counters.cpp`, add `armyDecisions` to `CounterSnapshot`, to `snapshot()`, and to whichever comparison and baseline-key list the file uses. Follow the existing pattern for `squadDecisions` exactly.

Add `armyDecisions=0` to both baseline files for now; Step 7 replaces it with the real value.

- [ ] **Step 4: Add `roleAnchorFor`**

Declare in `c++/src/Squads.hpp` (add `#include "Army.hpp"`):

```cpp
// Where this squad's role says it wants to stand, before terrain has an
// opinion. chooseTacticalObjective scores candidates AROUND this point, so
// this is what makes terrain awareness compose with coordination rather than
// override it.
//
// Parallel-safe in phase 4: reads its own squad, its target's and ward's
// centroids, and the army aggregate, all of which are read-only by then.
Vec2 roleAnchorFor(const SquadHot& squads, const ArmyHot& armies, size_t squadIndex);
```

Implement in `c++/src/Squads.cpp`:

```cpp
Vec2 roleAnchorFor(const SquadHot& squads, const ArmyHot& armies, size_t s) {
    const Vec2 C{ squads.centroidX[s], squads.centroidY[s] };
    const Vec2 F{ squads.facingX[s], squads.facingY[s] };
    const uint16_t tgt = squads.targetSquad[s];

    if (tgt >= squads.count || squads.memberCount[tgt] == 0) return C;
    const Vec2 T{ squads.centroidX[tgt], squads.centroidY[tgt] };
    const Vec2 toT = normalizeSafe({ T.x - C.x, T.y - C.y }, F);

    const size_t army = (size_t)squads.team[s];
    const Vec2 front{ armies.frontX[army], armies.frontY[army] };
    const Vec2 frontDir{ armies.frontDirX[army], armies.frontDirY[army] };

    switch ((SquadRole)squads.role[s]) {
        case SquadRole::Line:
            // Straight at the assigned enemy. Contact and the anchor latch are
            // what stop this from becoming a walk-through.
            return Vec2{ C.x + toT.x * kAdvanceLead, C.y + toT.y * kAdvanceLead };

        case SquadRole::Screen: {
            const uint16_t ward = squads.wardSquad[s];
            if (ward >= squads.count) return Vec2{ C.x + toT.x * kAdvanceLead,
                                                   C.y + toT.y * kAdvanceLead };
            const Vec2 W{ squads.centroidX[ward], squads.centroidY[ward] };
            const Vec2 wardToThreat = normalizeSafe({ T.x - W.x, T.y - W.y }, toT);
            // Stand off from the ward along the line to the threat. Being
            // BETWEEN them is the whole job, so the anchor is defined relative
            // to the ward rather than to ourselves.
            return Vec2{ W.x + wardToThreat.x * kScreenStandoff,
                         W.y + wardToThreat.y * kScreenStandoff };
        }

        case SquadRole::Flank: {
            // Approach the target from its side rather than its face. Both
            // perpendiculars are equally valid; pick the nearer one so cavalry
            // do not cross the whole field, and break the tie on the left.
            const Vec2 perpL{ -toT.y, toT.x };
            const Vec2 perpR{  toT.y, -toT.x };
            const Vec2 candL{ T.x + perpL.x * kFlankSweep, T.y + perpL.y * kFlankSweep };
            const Vec2 candR{ T.x + perpR.x * kFlankSweep, T.y + perpR.y * kFlankSweep };
            const float dL = (candL.x - C.x) * (candL.x - C.x) + (candL.y - C.y) * (candL.y - C.y);
            const float dR = (candR.x - C.x) * (candR.x - C.x) + (candR.y - C.y) * (candR.y - C.y);
            return (dR < dL) ? candR : candL;
        }

        case SquadRole::Shoot: {
            const float range = kUnitStats[(int)squads.unitType[s]].range;
            // Stand off inside range but not at its edge, so a target that
            // shuffles does not immediately walk out of reach.
            return Vec2{ T.x - toT.x * range * 0.85f, T.y - toT.y * range * 0.85f };
        }

        case SquadRole::Reserve:
        default:
            // Behind the army's own line, which is exactly what the front line
            // field exists to make expressible.
            return Vec2{ front.x - frontDir.x * kReserveDepth,
                         front.y - frontDir.y * kReserveDepth };
    }
}
```

Constants in `c++/src/Units.hpp`:

```cpp
// Role anchor geometry (design 7.3). All tuning knobs.
constexpr float kScreenStandoff = 45.0f;   // how far in front of its ward a screen stands
constexpr float kFlankSweep     = 90.0f;   // how wide of the target cavalry swing
constexpr float kReserveDepth   = 120.0f;  // how far behind the front line reserves wait
```

- [ ] **Step 5: Rewrite the decide**

In `c++/src/Squads.cpp`, add the `roleAnchor` parameter to `chooseTacticalObjective`. Change its signature in both header and source, and replace candidate 0:

```cpp
    // Candidate 0: the role's own anchor (design 7.5). Was the direct advance
    // point; now the role says where we want to be and terrain gets to argue.
    // A clear lane still wins, because a clear anchor scores best.
    cands.push_back(terrain.clearOfObstacles(roleAnchor));
```

Gate the archer standoff candidates on the role rather than on the unit type:

```cpp
    if (squads.role[s] == (uint8_t)SquadRole::Shoot) {
```

Now replace `selectTargetSquad` with `squadDecide`. Keep the enemy loop from Task 8 intact; replace the target selection and order assignment:

```cpp
void squadDecide(SquadHot& squads, const ArmyHot& armies, size_t s,
                 const TerrainField& terrain, float dt) {
    if (squads.memberCount[s] == 0) return;

    // --- Threat survey. One walk over enemy squads feeds everything below.
    // Reading other squads' centroids is safe HERE and only here: phase 2's
    // barrier has made every centroid read-only for the rest of the tick.
    float nearestSq = 1e30f;
    uint16_t nearest = UINT16_MAX;
    float nearestMeleeSq = 1e30f;
    uint8_t rear = 0;
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];

    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;

        if (d < nearestSq) { nearestSq = d; nearest = (uint16_t)e; }
        if (squads.unitType[e] != UnitType::Archer && d < nearestMeleeSq) {
            nearestMeleeSq = d;
        }
        if (d < kRallyRadius * kRallyRadius && (dx * fx + dy * fy) < 0.0f) rear = 1;
    }

    squads.nearestEnemyDist[s] = (nearestSq < 1e30f) ? std::sqrt(nearestSq) : 1e30f;
    squads.rearThreat[s] = rear;

    // Every enemy squad is wiped out. Hold, keep facing, stop pretending we
    // have somewhere to be.
    if (nearest == UINT16_MAX) {
        squads.order[s] = (uint8_t)SquadOrder::Hold;
        squads.objectiveX[s] = squads.centroidX[s];
        squads.objectiveY[s] = squads.centroidY[s];
        squads.moveX[s] = squads.facingX[s];
        squads.moveY[s] = squads.facingY[s];
        return;
    }

    // targetSquad comes from the commander (Army.cpp), NOT from picking the
    // nearest enemy. That change is the whole point of the army tier. Fall
    // back to nearest only if the commander has somehow left us pointed at a
    // squad that has since been annihilated.
    const uint16_t tgt = squads.targetSquad[s];
    if (tgt >= squads.count || squads.memberCount[tgt] == 0 ||
        squads.team[tgt] == squads.team[s]) {
        squads.targetSquad[s] = nearest;
    }

    // --- Order. Rout is owned by resolution (Morale.cpp) and must not be
    // overwritten here: a broken squad does not take orders.
    if (squads.order[s] != (uint8_t)SquadOrder::Rout) {
        if (squads.contact[s]) {
            // Contact halt (design 5.2). Overrides every role: a formation
            // that has met the enemy is fighting, whatever it was sent to do.
            squads.order[s] = (uint8_t)SquadOrder::Engaged;
        } else {
            switch ((SquadRole)squads.role[s]) {
                case SquadRole::Line:    squads.order[s] = (uint8_t)SquadOrder::Advance; break;
                case SquadRole::Screen:  squads.order[s] = (uint8_t)SquadOrder::Screen;  break;
                case SquadRole::Flank:   squads.order[s] = (uint8_t)SquadOrder::Flank;   break;
                case SquadRole::Shoot:   squads.order[s] = (uint8_t)SquadOrder::Advance; break;
                default:                 squads.order[s] = (uint8_t)SquadOrder::Hold;    break;
            }
        }
    }

    // --- Facing, slewed rather than snapped (Task 2).
    {
        const uint16_t t = squads.targetSquad[s];
        const float dx = squads.centroidX[t] - squads.centroidX[s];
        const float dy = squads.centroidY[t] - squads.centroidY[s];
        const Vec2 f = slewFacing(Vec2{ squads.facingX[s], squads.facingY[s] },
                                  Vec2{ dx, dy }, kFacingSlewRate * dt);
        squads.facingX[s] = f.x;
        squads.facingY[s] = f.y;
    }

    // --- Objective: the role says where, terrain gets to argue.
    const Vec2 anchor = roleAnchorFor(squads, armies, s);
    Vec2 obj{}, mv{};
    chooseTacticalObjective(terrain, squads, s, anchor, obj, mv);
    squads.objectiveX[s] = obj.x;
    squads.objectiveY[s] = obj.y;
    squads.moveX[s] = mv.x;
    squads.moveY[s] = mv.y;
}
```

Update the declaration in `c++/src/Squads.hpp`, replacing `selectTargetSquad`'s. Delete `selectTargetSquad` entirely rather than leaving it unused: an unused targeting function beside a live one is exactly the kind of thing a later reader wires back in by accident.

**`test_squads.cpp` calls `selectTargetSquad` directly.** Update those call sites to `squadDecide`, passing an `ArmyHot` with two `spawn()` calls and `dt` of `1.0f/60.0f`. The existing assertions about targeting-by-nearest no longer hold, because targets now come from the commander. Rewrite those cases to set `squads.targetSquad` first and assert the decide **respects** it, which is the new contract.

- [ ] **Step 6: Wire the phase into the tick**

In `c++/src/Simulation.hpp`, add the member and the phase:

```cpp
    // The army tier: exactly two entries, one per team.
    ArmyHot armies;
```

```cpp
    void phaseArmyDecide();     // Phase 3: serial, 2 entities.
```

Add `#include "Army.hpp"`.

In `c++/src/Simulation.cpp`'s `init()`, spawn the two armies (after squads are spawned, before the aggregate seeding):

```cpp
    // One army per team. Never destroyed, exactly like squads, so nothing
    // reading an army index ever needs a liveness check.
    armies = ArmyHot{};
    armies.spawn();   // Team::A
    armies.spawn();   // Team::B
```

Implement the phase:

```cpp
void Simulation::phaseArmyDecide() {
    // Serial: two entities make that free, and free serial execution makes
    // bit-reproducibility free too. Placement is forced -- after phase 2's
    // barrier, so every centroid is final, and before phase 4, so no squad
    // chooses an objective from a stale role.
    updateArmyAggregate(squads, armies);

    for (size_t a = 0; a < armies.count; ++a) {
        // Both armies decide unconditionally on the first tick. Without that,
        // the stagger below would let a squad act on a role it has never been
        // given. After that they alternate, so the two armies never re-decide
        // on the same tick and an assignment persists long enough to read.
        const bool firstTick = (tickNumber == 1u);
        if (firstTick || (tickNumber % kArmyDecideInterval) == a) {
            assignRoles(squads, armies, (Team)a);
            workCounters.add(workCounters.armyDecisions, 1);
        }
    }
}
```

In `tick()`, insert the phase and renumber the comments:

```cpp
    // Phase 3: serial, two entities. Reads every squad's finalized centroid
    // and writes each squad's role and target.
    phaseArmyDecide();

    // Phase 4: parallel over squads. Safe to read every squad's aggregate
    // only because phase 2's barrier made those values read-only.
    phaseSquadDecide(dt, rng);
    jobSystem.waitAll();
```

Update `phaseSquadDecide`'s body to call `squadDecide(squads, armies, s, terrain, dt)`.

- [ ] **Step 7: Run tests, regenerate baselines, commit**

Run: `scripts/build.bat -t`
Expected: the four new cases PASS, **including thread invariance**. That case is the one that matters most here; if it fails, the army phase or the decide is reading something it should not, and no baseline gets regenerated until it is understood.

`test_counters.cpp` FAILS on `stateDigest`, `squadDecisions`, and the new `armyDecisions`. Regenerate both baselines as in Task 2 Step 5, remembering to paste the seventh value, with this note:

```
# Regenerated 2026-08-22: squads take their targets from a new army tier
# instead of each picking the nearest enemy. Line squads spread across enemy
# squads by strength, archers are assigned to shoot, threatened archer
# squads get an infantry screen, and cavalry flank. armyDecisions is a new
# counter. Deliberate behavioural change.
```

```bash
git add c++/src/Squads.hpp c++/src/Squads.cpp c++/src/Simulation.hpp \
        c++/src/Simulation.cpp c++/src/Units.hpp c++/src/WorkCounters.hpp \
        c++/tests/test_army.cpp c++/tests/test_squads.cpp \
        c++/tests/test_counters.cpp c++/tests/baseline/
git commit -m "feat: drive squad targets and orders from the army tier

phaseArmyDecide sits between squad-aggregate and squad-decide, where its
placement is forced: after every centroid is final, before any squad picks
an objective. Serial, because two entities make that free.

selectTargetSquad is replaced by squadDecide, which takes the commander's
role, turns it into an order and a role anchor, and hands that anchor to
the terrain scorer. chooseTacticalObjective keeps its mechanism entirely
and now scores candidates around the role's anchor instead of always around
the direct lane, so terrain awareness composes with coordination.

Contact overrides every role: a formation that has met the enemy fights,
whatever it was sent to do. Rout overrides everything, since resolution
owns it and a broken squad does not take orders."
```

---

## Task 12: Arrow arc and both-team hit testing

Arrows currently skip same-team soldiers for their whole flight, so friendly fire is impossible and "avoid friendly fire" is not a behavior an archer could have.

The arc resolves a contradiction in the requirements. "Put infantry between yourself and the target" and "avoid friendly fire" are opposites under a flat trajectory, because your own screen is exactly what you would be shooting through. Under an arc they are consistent, for the same reason they were in reality: massed archery was indirect, so the danger to your own side came from where the arrows landed, not from where they were loosed.

**Files:**
- Modify: `c++/src/Projectiles.hpp`, `c++/src/Projectiles.cpp`, `c++/src/Units.hpp`
- Test: `c++/tests/test_projectiles.cpp`

**Interfaces:**
- Produces: `ProjectileHot::traveled`, `ProjectileHot::liveAfter`. `ProjectileHot::spawn` gains a trailing `float liveAfter` parameter.
- Task 20 mixes both into the digest.

- [ ] **Step 1: Add the fields and constant**

In `c++/src/Projectiles.hpp`, add to `ProjectileHot`:

```cpp
    // Arc model (design 8.1). An arrow is above head height until it has flown
    // `liveAfter` px, and hit-tests nothing until then. Once live it can hit
    // EITHER team: your own screen is under the arc and safe, but volleying
    // into a mixed melee kills your own men.
    std::vector<float> traveled;
    std::vector<float> liveAfter;
```

Update `spawn()` to take and store them:

```cpp
    void spawn(float px, float py, float vx, float vy, Team t, uint8_t dmg,
               float life, float armAfter) {
        // ... existing push_backs ...
        traveled.push_back(0.0f);
        liveAfter.push_back(armAfter);
        count++;
    }
```

Update `clear()` to clear both.

In `c++/src/Units.hpp`:

```cpp
// Fraction of the flight to the target that an arrow spends above head
// height. Below this it hits nothing at all, friend or foe.
//
// Two consequences that are correct rather than bugs. A point-blank shot has a
// tiny liveAfter and so is live almost immediately, which is right: close
// range archery is direct fire. And an arrow that MISSES stays live for the
// rest of its flight, so a long overshoot can still strike whatever is behind
// the target, on either side.
constexpr float kArrowArcFraction = 0.6f;
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_projectiles.cpp`:

```cpp
namespace {
// One arrow flying +x from the origin, with a single soldier of the given team
// planted at `fraction` of the way to a target `dist` away. Returns the arrow's
// intentHitTarget after flying far enough to reach that soldier.
uint32_t flyPast(Team arrowTeam, Team soldierTeam, float dist, float fraction) {
    SoldierHot soldiers;
    soldiers.spawn(dist * fraction, 0.0f, 0.0f, 0.0f, soldierTeam,
                   UnitType::Infantry, 0);

    SpatialHash hash(1280.0f, 720.0f, 50.0f);
    hash.insert(0u, soldiers.posX[0], soldiers.posY[0]);

    ProjectileHot p;
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, arrowTeam, kArrowDamage,
            kArrowLifetime, kArrowArcFraction * dist);

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    // Fly until past the soldier, or the arrow expires.
    for (int t = 0; t < 600 && p.posX[0] <= dist * fraction + 20.0f; ++t) {
        integrateProjectile(p, soldiers, hash, 0, dt, scratch);
        if (p.intentHitTarget[0] != UINT32_MAX) break;
    }
    return p.intentHitTarget[0];
}
} // namespace

TEST_CASE("an arrow passes harmlessly over anyone under its arc") {
    // 20 percent along, well inside kArrowArcFraction.
    CHECK(flyPast(Team::A, Team::A, 400.0f, 0.2f) == UINT32_MAX);
    CHECK(flyPast(Team::A, Team::B, 400.0f, 0.2f) == UINT32_MAX);
}

TEST_CASE("an arrow is live near the target and hits either team") {
    CHECK(flyPast(Team::A, Team::B, 400.0f, 0.95f) == 0u);
    // The whole point: your own men near the impact are NOT safe.
    CHECK(flyPast(Team::A, Team::A, 400.0f, 0.95f) == 0u);
}

TEST_CASE("a point-blank shot is live almost immediately") {
    // liveAfter scales with the shot distance, so close range is direct fire.
    CHECK(flyPast(Team::A, Team::B, 30.0f, 0.9f) == 0u);
}

TEST_CASE("traveled accumulates with flight, not with wall-clock ticks") {
    SoldierHot soldiers;
    SpatialHash hash(1280.0f, 720.0f, 50.0f);
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, kArrowSpeed, 0.0f, Team::A, kArrowDamage,
            kArrowLifetime, 1e9f);   // never arms, so it just flies

    std::vector<uint32_t> scratch;
    const float dt = 1.0f / 60.0f;
    for (int t = 0; t < 30; ++t) integrateProjectile(p, soldiers, hash, 0, dt, scratch);

    CHECK(p.traveled[0] == doctest::Approx(kArrowSpeed * dt * 30.0f).epsilon(1e-3));
}

TEST_CASE("compaction moves the arc fields with everything else") {
    ProjectileHot p;
    p.spawn(0.0f, 0.0f, 1.0f, 0.0f, Team::A, 1, 0.0f, 111.0f);   // expired
    p.spawn(5.0f, 0.0f, 1.0f, 0.0f, Team::B, 1, 1.0f, 222.0f);   // alive
    p.traveled[1] = 33.0f;

    compactProjectiles(p);
    REQUIRE(p.count == 1);
    CHECK(p.liveAfter[0] == doctest::Approx(222.0f));
    CHECK(p.traveled[0]  == doctest::Approx(33.0f));
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error, `spawn` takes 7 arguments, not 8.

- [ ] **Step 4: Write the implementation**

In `c++/src/Projectiles.cpp`, `spawnArrows` computes the arm distance from the shot it is actually taking:

```cpp
        out.spawn(px, py, rx * kArrowSpeed, ry * kArrowSpeed,
                  soldiers.team[i], kArrowDamage, kArrowLifetime,
                  kArrowArcFraction * dist);
```

`dist` is already computed above for the lead calculation, so this adds no work.

In `integrateProjectile`, accumulate `traveled` and gate the hit test. Replace the same-team skip:

```cpp
    // Distance flown this step, accumulated before the hit test so an arrow
    // that arms mid-step is live for the rest of that step rather than waiting
    // a full tick.
    const float stepX = p.velX[i] * dt;
    const float stepY = p.velY[i] * dt;
    p.traveled[i] += std::sqrt(stepX * stepX + stepY * stepY);

    // Under the arc: above head height, so it hits nothing at all. This is
    // what makes a friendly screen directly in front of the archer safe to
    // shoot over, which is the behaviour the whole positioning layer depends
    // on being possible.
    if (p.traveled[i] < p.liveAfter[i]) {
        p.intentHitTarget[i] = UINT32_MAX;
        return;
    }
```

and inside the neighbor loop, delete this line:

```cpp
        if (soldiers.team[n] == p.team[i]) continue;
```

replacing it with a comment so nobody restores it:

```cpp
        // NO team check. A live arrow hits whoever it crosses (design 8.1).
        // The shooter cannot hit itself, not by a special case but because it
        // is behind the arm distance by construction.
```

In `compactProjectiles`, add the two arrays to both the swap block and the `pop_back` block. Missing either desyncs the structure of arrays, which the compaction test above catches.

- [ ] **Step 5: Run tests, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: the five new cases PASS. Existing projectile tests that construct a `ProjectileHot` by hand need the new `spawn` argument; pass `0.0f` where a test wants an immediately-live arrow, which preserves each of those tests' original intent.

`test_counters.cpp` FAILS on `stateDigest`. Regenerate both baselines with:

```
# Regenerated 2026-08-22: arrows now fly an arc. They hit nothing for the
# first kArrowArcFraction of the flight to their target and then go live
# against BOTH teams, so friendly fire is possible for the first time.
```

```bash
git add c++/src/Projectiles.hpp c++/src/Projectiles.cpp c++/src/Units.hpp \
        c++/tests/test_projectiles.cpp c++/tests/baseline/
git commit -m "feat: give arrows an arc and let them hit either team

Arrows skipped same-team soldiers for the whole flight, so friendly fire
was impossible and avoiding it was not a behaviour an archer could have.

An arrow is now above head height for the first kArrowArcFraction of its
flight to the target and hits nothing at all, then goes live against both
teams. That resolves a contradiction in the requirements: putting infantry
between yourself and the target, and avoiding friendly fire, are opposites
under a flat trajectory and consistent under an arc.

A point-blank shot arms almost immediately, which is correct: close range
archery is direct fire."
```

---

## Task 13: Firing discipline while moving

The spread penalty for a moving shooter already exists. What is missing is any reason for an archer to stop: advancing forever is free today. A settle time makes standing still worth something, so holding a firing position becomes the archer's own preference rather than an instruction.

The same task adds the fire arc, which is how "cannot fire backward" gets expressed without a state check.

**Files:**
- Modify: `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/Projectiles.cpp`, `c++/src/Combat.cpp`, `c++/src/Units.hpp`
- Test: `c++/tests/test_projectiles.cpp`

**Interfaces:**
- Produces: `SoldierHot::steadyTimer`, incremented in `phaseMovementChunk`. Task 14 relies on the fire arc already being in place, so flight needs no separate "stop shooting" rule.

- [ ] **Step 1: Add the field and constants**

In `c++/src/Simulation.hpp`, add to `SoldierHot`, its `reserve()`, and its `spawn()`:

```cpp
    // Seconds spent below a walking pace. Archery accuracy needs a settled
    // shooter, and this is what makes standing still worth something.
    std::vector<float> steadyTimer;
```

```cpp
        steadyTimer.push_back(0.0f);
```

`compactDead` in `c++/src/Combat.cpp` must move it in the swap block and pop it in the pop block, exactly like `attackCooldown`.

In `c++/src/Units.hpp`:

```cpp
// Below this speed a soldier counts as standing still.
constexpr float kWalkSpeed = 6.0f;   // px/s

// How long an archer must be settled before it shoots at full accuracy.
constexpr float kSteadyTime = 0.8f;  // seconds

// Extra spread multiplier while unsettled, on top of the existing speed term.
// This is the number that makes a squad which keeps repositioning keep
// missing, and therefore the number that makes archers choose to hold still.
constexpr float kUnsettledSpreadMultiplier = 2.5f;

// An archer cannot loose at a target more than this far off its own movement
// direction while moving faster than a walk. Expressed as a cosine because
// that is what a dot product against a normalized heading gives directly.
//
// This is the whole of "cannot fire backward while fleeing", with no state
// check: flight points away from the enemy, so a fleeing archer's target is
// always behind it. An archer sidestepping slowly into position is under
// kWalkSpeed and unaffected, so it can still loose sideways.
constexpr float kMaxFireCos = 0.5f;   // 60 degrees
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_projectiles.cpp`:

```cpp
namespace {
// Spawns one arrow from a single archer at `speed` px/s with the given settle
// time, and returns how far off the true bearing it came out. Averaged over
// many soldier indices, since the spread roll is keyed on index.
float meanAimErrorPx(float speed, float steady) {
    SoldierHot soldiers;
    SquadHot squads;
    squads.spawn(Team::A, UnitType::Archer);
    squads.memberCount[0] = 64;
    squads.targetSquad[0] = 1;

    SquadHot enemy;  // placeholder so indices line up; unused directly

    // One target, straight ahead at a fixed range.
    const float range = 200.0f;
    soldiers.spawn(range, 0.0f, 0.0f, 0.0f, Team::B, UnitType::Infantry, 1);

    for (int k = 0; k < 64; ++k) {
        soldiers.spawn(0.0f, 0.0f, 0.0f, speed, Team::A, UnitType::Archer, 0);
        const size_t idx = soldiers.count - 1;
        soldiers.intentFire[idx] = 1;
        soldiers.steadyTimer[idx] = steady;
    }
    squads.targetSoldier[0] = 0u;
    squads.spawn(Team::B, UnitType::Infantry);
    squads.memberCount[1] = 1;

    ProjectileHot p;
    const Rng rng{ 42u, 7u };
    spawnArrows(soldiers, squads, p, rng);
    REQUIRE(p.count > 0);

    // Perpendicular deviation of each arrow's velocity from the +x bearing,
    // scaled to px at the target's range.
    float total = 0.0f;
    for (size_t i = 0; i < p.count; ++i) {
        const float len = std::sqrt(p.velX[i] * p.velX[i] + p.velY[i] * p.velY[i]);
        total += std::abs(p.velY[i] / len) * range;
    }
    return total / (float)p.count;
}
} // namespace

TEST_CASE("a settled archer shoots tighter than an unsettled one") {
    const float settled   = meanAimErrorPx(0.0f, kSteadyTime * 2.0f);
    const float unsettled = meanAimErrorPx(0.0f, 0.0f);
    CHECK(settled < unsettled);
}

TEST_CASE("a moving archer shoots wider than a stationary one") {
    const float still  = meanAimErrorPx(0.0f, kSteadyTime * 2.0f);
    const float moving = meanAimErrorPx(kUnitStats[(int)UnitType::Archer].speed,
                                        kSteadyTime * 2.0f);
    CHECK(moving > still);
}

TEST_CASE("steadyTimer accumulates while still and resets on movement") {
    Simulation sim(1280, 720, 42u);
    sim.init(600);
    sim.setPaused(false);
    for (int t = 0; t < 120; ++t) sim.tick(1.0f / 60.0f);

    bool sawSettled = false, sawUnsettled = false;
    for (size_t i = 0; i < sim.getAgentCount(); ++i) {
        if (sim.soldierSteadyTimer(i) > 0.0f) sawSettled = true;
        if (sim.soldierSteadyTimer(i) == 0.0f) sawUnsettled = true;
    }
    CHECK(sawSettled);
    CHECK(sawUnsettled);
}
```

Add the accessor to `c++/src/Simulation.hpp`:

```cpp
    float soldierSteadyTimer(size_t i) const { return soldiers.steadyTimer[i]; }
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: compile error on `steadyTimer` before the field is added, then the settle-time case FAILS because spread ignores it.

- [ ] **Step 4: Write the implementation**

In `c++/src/Simulation.cpp`'s `phaseMovementChunk`, beside the existing cooldown decay:

```cpp
        // Settle timer. A soldier below a walking pace is standing still for
        // archery purposes. This phase already writes only its own soldier, so
        // it is safe here alongside the cooldown decay.
        if (speed < kWalkSpeed) {
            soldiers.steadyTimer[i] += dt;
        } else {
            soldiers.steadyTimer[i] = 0.0f;
        }
```

`speed` is already computed just above for the direction update, so this is free.

In `c++/src/Projectiles.cpp`'s `spawnArrows`, fold the settle term into the spread:

```cpp
        // Accuracy degrades with range, with the archer moving, and with the
        // archer not yet having settled. The settle term is what actually
        // changes behaviour: without it, advancing forever costs nothing, so
        // no archer ever has a reason to hold a firing position.
        const float shooterSpeed = std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                                             soldiers.velY[i] * soldiers.velY[i]);
        const float maxRange = kUnitStats[(int)UnitType::Archer].range;
        const float maxSpeed = kUnitStats[(int)UnitType::Archer].speed;
        const float settleMul = (soldiers.steadyTimer[i] >= kSteadyTime)
                              ? 1.0f : kUnsettledSpreadMultiplier;
        int spreadMrad = (int)((float)kArrowBaseSpreadMrad
                             * (1.0f + dist / maxRange)
                             * (1.0f + shooterSpeed / maxSpeed)
                             * settleMul);
        if (spreadMrad < 1) spreadMrad = 1;
```

In `c++/src/Simulation.cpp`'s `phaseSoldierSteerChunk`, gate `intentFire` on the fire arc. Replace the archer block:

```cpp
        soldiers.intentFire[i] = 0;
        if (soldiers.unitType[i] == UnitType::Archer &&
            soldiers.attackCooldown[i] <= 0.0f &&
            soldiers.state[i] != SoldierState::Dead) {
            const uint16_t sq = soldiers.squadId[i];
            const uint32_t t = (sq < squads.count) ? squads.targetSoldier[sq] : UINT32_MAX;
            if (t != UINT32_MAX && (size_t)t < soldiers.count) {
                // Fire arc. An archer moving faster than a walk may not loose
                // at anything more than kMaxFireCos off its heading. This is
                // the whole of "cannot fire backward while fleeing": flight
                // points away from the enemy, so a fleeing archer's target is
                // behind it, and no state check is needed. A slow sidestep is
                // under kWalkSpeed and unaffected.
                const float vx = soldiers.velX[i];
                const float vy = soldiers.velY[i];
                const float speed = std::sqrt(vx * vx + vy * vy);

                bool arcOk = true;
                if (speed > kWalkSpeed) {
                    const float tx = soldiers.posX[t] - soldiers.posX[i];
                    const float ty = soldiers.posY[t] - soldiers.posY[i];
                    const float tlen = std::sqrt(tx * tx + ty * ty);
                    if (tlen > 1e-4f) {
                        const float cosAngle = (tx * vx + ty * vy) / (tlen * speed);
                        arcOk = (cosAngle >= kMaxFireCos);
                    }
                }
                if (arcOk) soldiers.intentFire[i] = 1;
            }
        }
```

Reading `soldiers.posX[t]` here is safe: positions are written in phases 8 and 9, never in phase 5.

- [ ] **Step 5: Run tests, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: the three new cases PASS. Regenerate both baselines with:

```
# Regenerated 2026-08-22: archery now needs a settled shooter. Spread is
# multiplied by kUnsettledSpreadMultiplier until steadyTimer reaches
# kSteadyTime, and an archer moving faster than a walk cannot loose at a
# target more than kMaxFireCos off its heading.
```

```bash
git add c++/src/Simulation.hpp c++/src/Simulation.cpp c++/src/Projectiles.cpp \
        c++/src/Combat.cpp c++/src/Units.hpp \
        c++/tests/test_projectiles.cpp c++/tests/baseline/
git commit -m "feat: require a settled shooter and gate fire on a forward arc

The moving-accuracy penalty already existed. What was missing was any
reason for an archer to stop: advancing forever cost nothing. Spread is now
multiplied while the shooter is unsettled, so a squad that keeps
repositioning keeps missing and holding a firing position becomes the
archer's own preference rather than an instruction.

The fire arc is how 'cannot fire backward' is expressed without a state
check. An archer above walking pace cannot loose more than 60 degrees off
its heading, and flight points away from the enemy, so a fleeing archer's
target is always behind it. A slow sidestep is under the walk threshold and
can still loose sideways."
```

---

## Task 14: Archer flight

The panic flip is **squad-local and evaluated every tick**, not a commander decision. Roles come from the army tier on a stagger; panic cannot wait for it. Same reasoning that puts rout in resolution rather than in the scorer: the commander decides what a squad is for, and the squad decides when it is about to die.

The role stays `Shoot` throughout, so a rallied squad resumes shooting without needing a new assignment.

**Files:**
- Modify: `c++/src/Squads.cpp`, `c++/src/Soldiers.hpp`, `c++/src/Soldiers.cpp`, `c++/src/Simulation.cpp`, `c++/src/Units.hpp`
- Test: `c++/tests/test_army.cpp`

**Interfaces:**
- Consumes: `nearestMeleeSq` from the threat survey in `squadDecide` (Task 11); `roleAnchorFor` (Task 11).
- Produces: `steerToward` gains a trailing `float speedScale = 1.0f` parameter. `roleAnchorFor` gains a `Withdraw` branch.

- [ ] **Step 1: Add the constants**

In `c++/src/Units.hpp`:

```cpp
// Archer flight (design 8.5). The panic radius is deliberately SMALLER than
// kScreenThreatRadius: the screen should already be in place by the time the
// archers would break for the rear. The gap between panic and rally is
// hysteresis, without which a squad at the boundary flips every tick.
constexpr float kArcherPanicRadius = 170.0f;
constexpr float kArcherRallyRadius = 280.0f;

// Archers drop their discipline and run. Faster than their marching speed,
// and faster than the infantry chasing them, or fleeing would be pointless.
constexpr float kFleeSpeedMultiplier = 1.45f;

// How far back a withdrawing squad aims, measured from its own position along
// the escape direction.
constexpr float kWithdrawDistance = 200.0f;
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_army.cpp`:

```cpp
TEST_CASE("archers break for the rear when melee closes, and stop shooting") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    bool sawWithdraw = false;
    for (int t = 0; t < 2000 && !sawWithdraw; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t s = 0; s < sim.getSquadCount(); ++s) {
            if (sim.squadUnitType(s) != UnitType::Archer) continue;
            if (sim.squadOrder(s) == (uint8_t)SquadOrder::Withdraw) {
                sawWithdraw = true;
                // The role is unchanged: they are still archers with a job,
                // just running. That is what lets them resume without a new
                // assignment when they rally.
                CHECK(sim.squadRole(s) == (uint8_t)SquadRole::Shoot);
                // And the objective is away from the threat, not toward it.
                const uint16_t threat = sim.squadTargetSquad(s);
                const float cx = sim.squadCentroidX(s), cy = sim.squadCentroidY(s);
                const float tx = sim.squadCentroidX(threat), ty = sim.squadCentroidY(threat);
                const float ox = sim.squadObjectiveX(s), oy = sim.squadObjectiveY(s);
                const float nowDist  = std::sqrt((cx - tx) * (cx - tx) + (cy - ty) * (cy - ty));
                const float goalDist = std::sqrt((ox - tx) * (ox - tx) + (oy - ty) * (oy - ty));
                CHECK(goalDist > nowDist);
                break;
            }
        }
    }
    CHECK(sawWithdraw);
}

TEST_CASE("a withdrawing squad's soldiers move faster than their march speed") {
    Simulation sim(1280, 720, 42u);
    sim.init(2000);
    sim.setPaused(false);

    const float march = kUnitStats[(int)UnitType::Archer].speed;
    bool sawFast = false;
    for (int t = 0; t < 2000 && !sawFast; ++t) {
        sim.tick(1.0f / 60.0f);
        for (size_t i = 0; i < sim.getAgentCount(); ++i) {
            if (sim.soldierUnitType(i) != UnitType::Archer) continue;
            const uint16_t sq = sim.soldierSquadId(i);
            if (sim.squadOrder(sq) != (uint8_t)SquadOrder::Withdraw) continue;
            if (sim.soldierSpeed(i) > march * 1.1f) { sawFast = true; break; }
        }
    }
    CHECK(sawFast);
}

TEST_CASE("hysteresis: panic entry and exit use different radii") {
    // Stated as a property of the constants rather than simulated, because the
    // failure mode is a squad flip-flopping every tick and the guard against
    // it is simply that the two radii differ.
    CHECK(kArcherRallyRadius > kArcherPanicRadius);
    CHECK(kArcherPanicRadius < kScreenThreatRadius);
}
```

Add these accessors to `c++/src/Simulation.hpp`:

```cpp
    uint16_t soldierSquadId(size_t i) const { return soldiers.squadId[i]; }
    float    soldierSpeed(size_t i) const {
        return std::sqrt(soldiers.velX[i] * soldiers.velX[i] +
                         soldiers.velY[i] * soldiers.velY[i]);
    }
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: `archers break for the rear` FAILS. No squad ever takes the `Withdraw` order.

- [ ] **Step 4: Add the panic branch to `squadDecide`**

The threat survey in `squadDecide` already computes `nearestMeleeSq`. Use it. Replace the order block's `Shoot` case:

```cpp
    if (squads.order[s] != (uint8_t)SquadOrder::Rout) {
        const float meleeDist = (nearestMeleeSq < 1e30f)
                              ? std::sqrt(nearestMeleeSq) : 1e30f;

        if (squads.contact[s]) {
            squads.order[s] = (uint8_t)SquadOrder::Engaged;
        } else if ((SquadRole)squads.role[s] == SquadRole::Shoot) {
            // Panic is squad-local and evaluated EVERY tick, not on the army
            // stagger. Roles say what a squad is for; this says when it is
            // about to die, and that cannot wait up to kArmyDecideInterval
            // ticks. Same argument that puts rout in resolution.
            //
            // Entry and exit use different radii. The gap is hysteresis:
            // without it a squad sitting at the boundary flips every tick.
            const bool alreadyFleeing =
                (squads.order[s] == (uint8_t)SquadOrder::Withdraw);
            const float threshold = alreadyFleeing ? kArcherRallyRadius
                                                   : kArcherPanicRadius;
            squads.order[s] = (meleeDist < threshold)
                            ? (uint8_t)SquadOrder::Withdraw
                            : (uint8_t)SquadOrder::Advance;
        } else {
            switch ((SquadRole)squads.role[s]) {
                case SquadRole::Line:   squads.order[s] = (uint8_t)SquadOrder::Advance; break;
                case SquadRole::Screen: squads.order[s] = (uint8_t)SquadOrder::Screen;  break;
                case SquadRole::Flank:  squads.order[s] = (uint8_t)SquadOrder::Flank;   break;
                default:                squads.order[s] = (uint8_t)SquadOrder::Hold;    break;
            }
        }
    }
```

Add the escape anchor to `roleAnchorFor`. It keys on the **order**, not the role, because the role stays `Shoot` while fleeing. Put this check first, before the role switch:

```cpp
    // Withdraw and Rout both run, and both key on ORDER rather than role: a
    // fleeing archer squad is still a Shoot squad, which is exactly what lets
    // it resume its job when it rallies without a new assignment.
    if (squads.order[s] == (uint8_t)SquadOrder::Withdraw ||
        squads.order[s] == (uint8_t)SquadOrder::Rout) {
        // Away from the threat, and biased toward our own rear so fleeing
        // archers run toward protection rather than into a corner. The army
        // front line is what makes "our own rear" expressible at all.
        const Vec2 away = normalizeSafe({ C.x - T.x, C.y - T.y },
                                        Vec2{ -frontDir.x, -frontDir.y });
        const float ax = away.x - frontDir.x;
        const float ay = away.y - frontDir.y;
        const Vec2 escape = normalizeSafe({ ax, ay }, away);
        return Vec2{ C.x + escape.x * kWithdrawDistance,
                     C.y + escape.y * kWithdrawDistance };
    }
```

`frontDir` is already read at the top of `roleAnchorFor`; move that read above this block if it currently sits below.

- [ ] **Step 5: Make fleeing soldiers actually faster**

In `c++/src/Soldiers.hpp`, add a defaulted parameter so no existing caller changes:

```cpp
// `speedScale` multiplies the unit's base speed. Used for flight: a routing or
// withdrawing squad drops its discipline and runs. Defaulted so existing
// callers and tests are untouched.
void steerToward(SoldierHot& soldiers, size_t soldierIndex, Vec2 target, float dt,
                 float speedScale = 1.0f);
```

In `c++/src/Soldiers.cpp`:

```cpp
    const float speed = kUnitStats[(int)soldiers.unitType[i]].speed * speedScale;
```

The `approach` easing below already derives from `speed`, so it scales correctly with no further change.

In `c++/src/Simulation.cpp`'s `phaseSoldierSteerChunk`, pass the scale:

```cpp
        {
            const uint16_t sq = soldiers.squadId[i];
            // Withdrawing and routing squads run. Keyed on order rather than
            // on role, so it covers both an ordered retreat and a break.
            const uint8_t ord = squads.order[sq];
            const float speedScale =
                (ord == (uint8_t)SquadOrder::Withdraw || ord == (uint8_t)SquadOrder::Rout)
                ? kFleeSpeedMultiplier : 1.0f;
            steerToward(soldiers, i,
                        clearOfObstacles(slotWorldPosition(squads, sq, soldiers.slotIndex[i],
                                                           squads.memberCount[sq])),
                        dt, speedScale);
        }
```

- [ ] **Step 6: Run tests, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: the three new cases PASS. Regenerate both baselines with:

```
# Regenerated 2026-08-22: archer squads now break for the rear when melee
# closes inside kArcherPanicRadius, run at kFleeSpeedMultiplier while
# withdrawing, and rally at the wider kArcherRallyRadius.
```

```bash
git add c++/src/Squads.cpp c++/src/Soldiers.hpp c++/src/Soldiers.cpp \
        c++/src/Simulation.hpp c++/src/Simulation.cpp c++/src/Units.hpp \
        c++/tests/test_army.cpp c++/tests/baseline/
git commit -m "feat: make archers run from closing melee

The panic flip is squad-local and evaluated every tick rather than on the
army stagger. Roles say what a squad is for; this says when it is about to
die, and that cannot wait up to kArmyDecideInterval ticks.

The role stays Shoot throughout, so a rallied squad resumes its job without
needing a new assignment. Entry and exit use different radii so a squad at
the boundary does not flip every tick.

Withdrawing and routing squads move at kFleeSpeedMultiplier, and their
escape anchor is biased toward the army's own rear so they run toward
protection rather than into a corner. No new rule is needed to stop them
shooting backward: task 13's fire arc already forbids it."
```

---

## Task 15: Screening and friendly-fire-aware targeting

Two scorer terms and one hold-fire rule. Both terms extend the existing candidate scorer rather than introducing a second mechanism.

Both are evaluated at squad centroid granularity. That is correct rather than merely cheap: an archer squad decides where to stand as a unit, and per-soldier lane tests would produce a formation that disagrees with itself about where to be.

**The hold-fire check has a phase-ordering constraint.** `selectTargetSoldier` runs in phase 2, where other squads' centroids are concurrently being written, so it cannot compute friendly-fire risk itself. The flag is computed in phase 4, where cross-squad reads are safe, and read on the **next** tick. One tick of lag, deliberately.

**Files:**
- Modify: `c++/src/Squads.hpp`, `c++/src/Squads.cpp`, `c++/src/Units.hpp`
- Test: `c++/tests/test_army.cpp`

**Interfaces:**
- Consumes: `chooseTacticalObjective`'s candidate loop (Task 11).
- Produces: `SquadHot::friendlyNearTarget` (uint8), written in phase 4 and read by `selectTargetSoldier` in phase 2 of the following tick.

- [ ] **Step 1: Add the field and constants**

In `c++/src/Squads.hpp`, add to `SquadHot` and initialize in `spawn()`:

```cpp
    // Set when this squad's target sits in a melee containing our own men, so
    // shooting at it would drop arrows on friends (design 8.3).
    //
    // Written in phase 4, where reading other squads' centroids is safe, and
    // read by selectTargetSoldier in phase 2 of the NEXT tick, where it is
    // not. That one tick of lag is the price of the phase ordering and is
    // harmless: squads do not teleport in 16ms.
    std::vector<uint8_t> friendlyNearTarget;
```

```cpp
        friendlyNearTarget.push_back(0);
```

In `c++/src/Units.hpp`:

```cpp
// Archer positioning scorer terms (design 8.2).
constexpr float kScreenBonusWeight       = 30.0f;  // reward standing behind our own line
constexpr float kScreenCorridorHalfWidth = 60.0f;  // how wide the "behind them" corridor is
constexpr float kFriendlyFireWeight      = 45.0f;  // penalty per friendly squad near the impact
constexpr float kMeleeMixRadius          = 70.0f;  // how close to the target counts as mixed in
```

- [ ] **Step 2: Write the failing test**

Add to `c++/tests/test_army.cpp`:

```cpp
TEST_CASE("archers prefer a firing position with friendly infantry in front") {
    // Two otherwise-equal candidate positions, one screened by our own line.
    // The scorer must pick the screened one.
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    f.add(Team::A, UnitType::Infantry, 300.0f, 300.0f, 30);   // squarely in front
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 700.0f, 300.0f, 30);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;   // no obstacles: isolates the screen term
    const Vec2 anchor = roleAnchorFor(f.squads, f.armies, archers);
    Vec2 obj{}, mv{};
    chooseTacticalObjective(empty, f.squads, archers, anchor, obj, mv);

    // The chosen objective must be on our side of the friendly infantry, not
    // out past it toward the enemy.
    CHECK(obj.x < 300.0f);
}

TEST_CASE("a squad holds fire when its target is mixed in with our own men") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 400.0f, 300.0f, 30);
    // Our own infantry right on top of the enemy: a melee.
    f.add(Team::A, UnitType::Infantry, 400.0f + kMeleeMixRadius * 0.4f, 300.0f, 30);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;
    squadDecide(f.squads, f.armies, archers, empty, 1.0f / 60.0f);
    CHECK(f.squads.friendlyNearTarget[archers] == 1);
}

TEST_CASE("a squad with a clean shot does not hold fire") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 400.0f, 300.0f, 30);
    f.add(Team::A, UnitType::Infantry, 150.0f, 300.0f, 30);   // well behind the impact
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    updateArmyAggregate(f.squads, f.armies);

    TerrainField empty;
    squadDecide(f.squads, f.armies, archers, empty, 1.0f / 60.0f);
    CHECK(f.squads.friendlyNearTarget[archers] == 0);
}

TEST_CASE("holding fire actually stops the squad acquiring a soldier target") {
    ArmyFixture f;
    const uint16_t archers = f.add(Team::A, UnitType::Archer, 100.0f, 300.0f, 12);
    const uint16_t foe = f.add(Team::B, UnitType::Infantry, 200.0f, 300.0f, 4);
    f.squads.role[archers] = (uint8_t)SquadRole::Shoot;
    f.squads.targetSquad[archers] = foe;
    f.squads.friendlyNearTarget[archers] = 1;

    SoldierHot soldiers;
    std::vector<uint32_t> members;
    f.squads.memberStart[foe] = 0;
    for (uint32_t k = 0; k < 4; ++k) {
        soldiers.spawn(200.0f, 300.0f, 0.0f, 0.0f, Team::B, UnitType::Infantry, foe);
        soldiers.slotIndex[k] = (uint16_t)k;
        members.push_back(k);
    }

    selectTargetSoldier(soldiers, f.squads, members, archers);
    CHECK(f.squads.targetSoldier[archers] == UINT32_MAX);
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `scripts/build.bat -t`
Expected: the hold-fire cases FAIL; `friendlyNearTarget` is never written.

- [ ] **Step 4: Add the scorer terms**

In `c++/src/Squads.cpp`, add two helpers to the anonymous namespace:

```cpp
// How many friendly squads sit near enough to the target to be caught by a
// volley aimed at it. Squad-centroid granularity on purpose: an archer squad
// decides where to shoot as a unit, and a per-soldier test would give a
// formation that disagreed with itself.
uint32_t friendlySquadsNear(const SquadHot& q, size_t self, Vec2 point, float radius) {
    uint32_t n = 0;
    const float rSq = radius * radius;
    for (size_t o = 0; o < q.count; ++o) {
        if (o == self) continue;
        if (q.team[o] != q.team[self]) continue;
        if (q.memberCount[o] == 0) continue;
        // Archers are not a screen and are not what a volley is aimed past.
        if (q.unitType[o] == UnitType::Archer) continue;
        const float dx = q.centroidX[o] - point.x;
        const float dy = q.centroidY[o] - point.y;
        if (dx * dx + dy * dy <= rSq) n++;
    }
    return n;
}

// Whether a friendly melee squad stands inside the corridor from `from` to
// `to`, which is what "we have infantry between us and them" means.
bool friendlyScreenBetween(const SquadHot& q, size_t self, Vec2 from, Vec2 to) {
    for (size_t o = 0; o < q.count; ++o) {
        if (o == self) continue;
        if (q.team[o] != q.team[self]) continue;
        if (q.memberCount[o] == 0) continue;
        if (q.unitType[o] == UnitType::Archer) continue;
        const Vec2 c{ q.centroidX[o], q.centroidY[o] };
        const Vec2 onLane = closestOnSegment(from, to, c);
        const float dx = c.x - onLane.x;
        const float dy = c.y - onLane.y;
        if (dx * dx + dy * dy <= kScreenCorridorHalfWidth * kScreenCorridorHalfWidth) {
            return true;
        }
    }
    return false;
}
```

In `chooseTacticalObjective`'s scoring loop, extend the archer branch:

```cpp
        if (squads.role[s] == (uint8_t)SquadRole::Shoot) {
            const float dT = std::sqrt((p.x - T.x) * (p.x - T.x) + (p.y - T.y) * (p.y - T.y));
            if (dT <= unitRange) score += kArcherStandoffWeight;
            else                 score -= 0.5f * kArcherStandoffWeight;
            if (terrain.segmentBlocked(p, T)) score -= kArcherStandoffWeight;

            // Stand behind our own line where we can. The arc (design 8.1) is
            // what makes this safe: a friendly screen directly in front is
            // under the arrows, not in their way.
            if (friendlyScreenBetween(squads, s, p, T)) score += kScreenBonusWeight;

            // And do not stand somewhere whose impact zone is full of our own
            // men. Positioning controls what is in front of you; this term is
            // the half of the problem positioning can address.
            score -= kFriendlyFireWeight
                   * (float)friendlySquadsNear(squads, s, T, kMeleeMixRadius);

            const float side = std::abs(rel.x * toT.y - rel.y * toT.x);
            score += 0.05f * side;
        } else if (isCavalry) {
```

Note the branch now keys on `role`, not on `isArcher`. Remove the now-unused `isArcher` local, or leave it if `unitRange` still uses it.

- [ ] **Step 5: Add the hold-fire flag and check**

At the end of `squadDecide`, after the objective is written:

```cpp
    // Friendly-fire hold (design 8.3). Where you stand controls what is in
    // front of you; what you SHOOT AT controls what is around the impact, and
    // no amount of repositioning fixes a target standing in our own melee.
    //
    // Computed here in phase 4 because it reads other squads' centroids.
    // selectTargetSoldier consumes it in phase 2 of the next tick, where such
    // a read would race.
    {
        const uint16_t t = squads.targetSquad[s];
        if (t < squads.count && squads.memberCount[t] > 0) {
            const Vec2 T2{ squads.centroidX[t], squads.centroidY[t] };
            squads.friendlyNearTarget[s] =
                (friendlySquadsNear(squads, s, T2, kMeleeMixRadius) > 0) ? 1 : 0;
        } else {
            squads.friendlyNearTarget[s] = 0;
        }
    }
```

In `selectTargetSoldier`, honor it:

```cpp
    // Hold fire rather than volleying into our own line. The flag was computed
    // last tick in phase 4; see its declaration for why it cannot be computed
    // here.
    if (squads.friendlyNearTarget[s]) return;
```

Place it after the existing `range <= 0.0f` early return, so melee squads are unaffected.

- [ ] **Step 6: Run tests, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: the four new cases PASS. Regenerate both baselines with:

```
# Regenerated 2026-08-22: archer squads now prefer firing positions screened
# by friendly infantry, avoid positions whose impact zone holds our own men,
# and hold fire outright when their target is mixed into a friendly melee.
```

```bash
git add c++/src/Squads.hpp c++/src/Squads.cpp c++/src/Units.hpp \
        c++/tests/test_army.cpp c++/tests/baseline/
git commit -m "feat: score archer positions for screening and friendly fire

Two terms on the existing candidate scorer rather than a second mechanism:
a bonus for standing behind our own melee line, and a penalty for a position
whose impact zone holds friendly squads. The arc is what makes the first one
safe, since a screen directly in front is under the arrows.

Positioning cannot solve the whole problem, so targeting takes the other
half: a squad whose target sits in a melee full of our own men holds fire.
That flag is computed in phase 4, where cross-squad reads are safe, and
consumed in phase 2 of the next tick, where they are not. The tick of lag is
the price of the phase ordering and is harmless at 16ms."
```

---

## Task 16: Digest coverage for every new field

Every field added across Tasks 3 through 15 is written by a phase and read by another. A field that is not in the digest is a field the thread-invariance gate is **blind to**: that phase could diverge across worker counts and every test would still pass.

The existing digest already carries this argument in its own comments, twice. This task discharges it for the fields this plan added.

**Files:**
- Modify: `c++/src/Simulation.cpp`
- Test: `c++/tests/test_determinism.cpp`

**Interfaces:**
- Consumes: every field added by Tasks 3, 4, 7, 10, 12, 13, 15.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_determinism.cpp`:

```cpp
TEST_CASE("thread invariance holds deep into a battle, not just early") {
    // 300 ticks is before first contact at 2000 agents. The phases this plan
    // added only do anything once squads engage, so a gate that stops at 300
    // never exercises them at all.
    auto run = [](uint32_t threads) {
        Simulation sim(1280, 720, 42u, threads);
        sim.init(2000);
        sim.setPaused(false);
        for (int t = 0; t < 1500; ++t) sim.tick(1.0f / 60.0f);
        return sim.stateDigest();
    };
    const uint64_t single = run(1u);
    CHECK(run(2u)  == single);
    CHECK(run(8u)  == single);
    CHECK(run(15u) == single);
}

TEST_CASE("the digest responds to every field this plan added") {
    // A field missing from the digest is a field the thread-invariance gate is
    // blind to. This checks the digest is at least SENSITIVE to squad and
    // projectile state by perturbing a run and requiring the value to move.
    Simulation a(1280, 720, 42u);
    a.init(2000);
    a.setPaused(false);
    for (int t = 0; t < 900; ++t) a.tick(1.0f / 60.0f);

    Simulation b(1280, 720, 42u);
    b.init(2000);
    b.setPaused(false);
    for (int t = 0; t < 901; ++t) b.tick(1.0f / 60.0f);

    CHECK(a.stateDigest() != b.stateDigest());
}
```

- [ ] **Step 2: Run test to verify it fails or passes**

Run: `scripts/build.bat -t`
Expected: both cases likely PASS already. **That is not evidence the digest is complete** and this step is not the check. Step 3 is a code audit, not a test.

- [ ] **Step 3: Extend the digest**

In `c++/src/Simulation.cpp`'s `stateDigest()`, add to the soldier loop:

```cpp
        // Written by phaseMovementChunk, read by spawnArrows. In the digest
        // for the reason the fields above it are: the gate must exercise it.
        d.mix(soldiers.steadyTimer[i]);
```

to the squad loop:

```cpp
        // Contact, anchor, and role state (this plan). Every one is written by
        // a phase and read by another, so a divergence in any of them across
        // worker counts would otherwise be invisible to the gate.
        d.mix(static_cast<uint32_t>(squads.contact[s]));
        d.mix(squads.contactTimer[s]);
        d.mix(squads.anchorX[s]);
        d.mix(squads.anchorY[s]);
        d.mix(static_cast<uint32_t>(squads.role[s]));
        d.mix(static_cast<uint32_t>(squads.wardSquad[s]));
        d.mix(static_cast<uint32_t>(squads.rearThreat[s]));
        d.mix(squads.nearestEnemyDist[s]);
        d.mix(squads.rallyTimer[s]);
        d.mix(static_cast<uint32_t>(squads.friendlyNearTarget[s]));
```

to the projectile loop:

```cpp
        d.mix(projectiles.traveled[i]);
        d.mix(projectiles.liveAfter[i]);
```

and add an army block after the projectile block:

```cpp
    // The army tier is written by a serial phase, so it cannot diverge on
    // thread count by construction. It is digested anyway, for the same reason
    // the squad tier was digested before it had live fields: so the gate
    // already covers it the day anything about that phase becomes parallel.
    d.mix(static_cast<uint32_t>(armies.count));
    for (size_t a = 0; a < armies.count; ++a) {
        d.mix(armies.strengthInfantry[a]);
        d.mix(armies.strengthArcher[a]);
        d.mix(armies.strengthCavalry[a]);
        d.mix(armies.centroidX[a]);
        d.mix(armies.centroidY[a]);
        d.mix(armies.frontX[a]);
        d.mix(armies.frontY[a]);
        d.mix(armies.frontDirX[a]);
        d.mix(armies.frontDirY[a]);
        d.mix(static_cast<uint32_t>(armies.posture[a]));
    }
```

- [ ] **Step 4: Audit every new field against three lists**

This is a manual check, and it is the actual deliverable of this task. For each field below, confirm it is (a) initialized in its struct's `spawn()`, (b) moved by compaction if its struct has one, and (c) mixed into the digest.

| Field | Struct | spawn() | compaction | digest |
|---|---|---|---|---|
| `contact`, `contactTimer` | SquadHot | yes | n/a | yes |
| `anchorX`, `anchorY` | SquadHot | yes | n/a | yes |
| `rearThreat`, `nearestEnemyDist`, `rallyTimer` | SquadHot | yes | n/a | yes |
| `role`, `wardSquad` | SquadHot | yes | n/a | yes |
| `friendlyNearTarget` | SquadHot | yes | n/a | yes |
| `steadyTimer` | SoldierHot | yes | **`compactDead`** | yes |
| `traveled`, `liveAfter` | ProjectileHot | yes | **`compactProjectiles`** | yes |
| every `ArmyHot` field | ArmyHot | yes | n/a | yes |

The two bolded entries are the ones with a real failure mode: a soldier or projectile array that compaction does not move desyncs the structure of arrays, and the corruption shows up as a wrong value on an unrelated agent. Task 12's compaction test covers the projectile pair. **Add the soldier equivalent now** to `c++/tests/test_combat.cpp`:

```cpp
TEST_CASE("compactDead moves steadyTimer with the rest of the soldier") {
    SoldierHot s;
    std::vector<float> prevX, prevY;
    s.spawn(0.0f, 0.0f, 0.0f, 0.0f, Team::A, UnitType::Archer, 0);
    s.spawn(5.0f, 0.0f, 0.0f, 0.0f, Team::A, UnitType::Archer, 0);
    prevX.assign(2, 0.0f);
    prevY.assign(2, 0.0f);

    s.state[0] = SoldierState::Dead;
    s.steadyTimer[1] = 1.25f;

    compactDead(s, prevX, prevY);
    REQUIRE(s.count == 1);
    CHECK(s.steadyTimer[0] == doctest::Approx(1.25f));
}
```

- [ ] **Step 5: Run tests, regenerate, commit**

Run: `scripts/build.bat -t`
Expected: everything PASSES, including both deep thread-invariance cases. `test_counters.cpp` FAILS on `stateDigest` only, because the digest now hashes more fields. **No behavior changed**, so this is the third legitimate reason a digest moves, the one the baseline file's own header describes.

Regenerate both baselines with:

```
# Regenerated 2026-08-22: the digest now covers the contact, anchor, morale,
# role, settle-timer, arrow-arc, and army-tier fields. NO behavioural change
# in this commit: the digest hashes more state, which is the third
# legitimate reason it moves, described above.
```

```bash
git add c++/src/Simulation.cpp c++/tests/test_determinism.cpp \
        c++/tests/test_combat.cpp c++/tests/baseline/
git commit -m "test: cover every new field in the state digest

A field outside the digest is a field the thread-invariance gate is blind
to: its phase could diverge across worker counts with every test still
green. This discharges that for the contact, anchor, morale, role,
settle-timer, arrow-arc, and army-tier fields.

The thread-invariance gate is also extended from 300 to 1500 ticks. At 2000
agents the first contact is well past 300, so the phases this work added
were never being exercised by the gate at all.

The army tier is digested despite being written serially, for the reason
the squad tier was digested before it had live fields: so the gate covers
it the day that changes.

Digest moves without behaviour changing. That is the third legitimate case
the baseline header describes."
```

---

## Task 17: Tuning pass and republished numbers

The design carries this as its own stage rather than as an afterthought, because two new scorer terms joined an already hand-tuned scorer and the weights need checking against observed behavior rather than against tests.

This task ends the plan, and it is the only one whose acceptance criteria are visual.

**Files:**
- Modify: `c++/src/Units.hpp` (weights only), `README.md`

- [ ] **Step 1: Watch it run**

```bash
scripts/build.bat -r
```

Check each success criterion from the design's section 14 by eye, at 2000 and at 10000 agents:

1. Two infantry formations meeting produce a line, not a blob. Front ranks fight, rear ranks close up, ranks stay distinguishable, neither centroid passes through the other.
2. A shaken squad is visibly less ordered than a fresh one, with no change to its formation shape.
3. Infantry are observably positioned between friendly archers and advancing threats, and the army does not converge on one enemy squad.
4. Archers hold position to shoot, retreat when infantry closes, run faster while retreating, and do not fire behind themselves while running.
5. Arrows loosed over a friendly front rank do not harm it; arrows into a mixed melee sometimes do.

- [ ] **Step 2: Tune only the weights, and only against what you saw**

The knobs, in the order most likely to need moving:

| Symptom | Knob |
|---|---|
| Squads freeze in contact against nothing | `kContactFraction` up, `kContactClearSeconds` down |
| Formations still interpenetrate | `kContactRadius` up |
| Shaken squads collapse into a point | `kMinCompression` up |
| Archers never stop moving | `kUnsettledSpreadMultiplier` up, `kSteadyTime` down |
| Archers panic constantly | `kArcherPanicRadius` down |
| Archers never get screened | `kScreenThreatRadius` up, `kScreenBonusWeight` up |
| Whole army still piles onto one enemy | check `assignRoles` step 5, not a weight |
| Cavalry wander uselessly | `kFlankSweep` down |

**Do not change structure in this task.** If behavior is wrong in a way no weight fixes, that is a finding to report, not a refactor to perform here.

- [ ] **Step 3: Re-run the full suite and regenerate baselines a final time**

```bash
scripts/build.bat -t
```

Every test must pass, including both deep thread-invariance cases. Regenerate both baseline files with a note naming the tuning pass.

- [ ] **Step 4: Measure and republish**

```bash
build/Release/tactix_bench.exe --agents 10000 --ticks 2000 --seed 42 --json
```

Update the table in `README.md` with the measured p50, p95, p99, max, and the new state digest.

**The README makes reproducibility claims, so the increase must be attributed, not just reported.** Replace the paragraph explaining the previous rise with one naming the cause. To get the attribution number, measure once with `phaseContact`'s body commented out and once with it live, on the same build, and quote both. That is the same method the existing paragraph used for `kArrowHitChancePct`, and it is what makes the claim checkable rather than asserted.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Units.hpp c++/tests/baseline/ README.md
git commit -m "tune: weight pass over contact, cohesion, and archer scoring

Weights only, no structural change, checked against observed behaviour at
2000 and 10000 agents rather than against tests.

Republishes the benchmark numbers. The tick cost rise is attributed to
phaseContact by direct measurement, with and without its body, on the same
build. That is the method the existing arrow-hit-chance paragraph used, and
it is what makes the claim checkable instead of asserted."
```

---

## Plan Self-Review

Checked after writing, against the spec.

**Spec coverage.** Every section maps to a task: 5.1 to Task 3, 5.2 to Task 4, 5.3 to Task 5, 5.4 to Task 6, 5.5 needs no task (it falls out of Task 5's geometry, as the spec argues), 5.6 to Task 2, 6 to Tasks 7 and 8, 7.1 to Task 9, 7.2 to Task 11, 7.3 and 7.4 to Task 10, 7.5 to Task 11, 8.1 to Task 12, 8.2 to Task 15, 8.3 to Task 15, 8.4 to Task 13, 8.5 to Task 14, 9 to Tasks 5 and 11, 10 to the file map, 11 across every task, 12 to Task 17.

**One gap found and closed.** The spec's section 11.1 asks for a rank-order test under compression, and I had it only as a formation unit test. Task 6 Step 2 now carries `compression preserves rank ORDER, only rank spacing`, which is the case that catches a shaken engaged squad collapsing to a point (the spec's own section 12 risk).

**Two spec deviations, both flagged at the top of this plan** rather than left for an implementer to discover: seconds instead of ticks for the two timers, and `slotWorldPosition` always reading the anchor instead of branching.

**Type consistency.** `squadDecide` replaces `selectTargetSquad` in Task 11 and every later reference uses the new name. `chooseTacticalObjective` gains its `roleAnchor` parameter in Task 11 and Task 15 uses that signature. `steerToward`'s `speedScale` is defaulted in Task 14, so Tasks 1 through 13 need no edit. `ProjectileHot::spawn` gains its eighth parameter in Task 12, and that task explicitly calls out fixing the existing tests that construct arrows.

**Ordering constraint worth restating.** Task 15's hold-fire flag is written in phase 4 and read in phase 2 of the following tick. An implementer who "fixes" that lag by moving the computation into `selectTargetSoldier` reintroduces a genuine data race that no test will catch, because the race is benign at one thread. The comment on the field says so; the reviewer should check it survives.

---

## Execution Handoff

Plan complete and saved to [`docs/superpowers/plans/2026-08-22-engagement-and-unit-ai.md`](2026-08-22-engagement-and-unit-ai.md).

Seventeen tasks in five stages. Stage 1 (Tasks 1 to 6) alone fixes the most visible failure and is independently shippable.
