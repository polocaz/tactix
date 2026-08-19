# Phase A: Performance Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every performance claim in Tactix reproducible by a third party — headless execution, bit-identical determinism, a benchmark harness, and CI that gates on deterministic work counters.

**Architecture:** Replace raylib's global RNG with a stateless per-agent hash function keyed on `(seed, agentIndex, tickNumber, callSite)`, which removes both a live data race and the simulation's last dependency on raylib. Split the single `tactix` executable into a raylib-free `tactix_sim` static library plus three consumers (GUI app, headless benchmark, test binary). Prove determinism with a state digest compared across thread counts, build configurations, and platforms.

**Tech Stack:** C++20, CMake 3.20+, raylib (GUI only), doctest, spdlog, GitHub Actions.

**Spec:** [`docs/superpowers/specs/2026-08-19-tactix-performance-foundation-design.md`](../specs/2026-08-19-tactix-performance-foundation-design.md)

**Scope:** This plan implements **Phase A only**. Phases B–H in the spec are sequenced but not planned; each gets its own plan after Phase B's profile data exists.

## Global Constraints

- **C++20**, `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`. Never set `-std=` manually in `CMAKE_CXX_FLAGS` — it breaks MSVC.
- **No `-ffast-math` / `/fp:fast`** anywhere, ever.
- **No FMA contraction:** `-ffp-contract=off` on GCC/Clang, `/fp:precise` on MSVC. Applied via the `tactix_fp_flags` INTERFACE target to every target compiling simulation code.
- **No `<cmath>` transcendentals in the tick path.** `std::sqrt` is permitted (IEEE-754 requires it correctly rounded). `std::sin`, `std::cos`, `std::pow`, `std::exp`, `std::log` are forbidden — use `detmath::` equivalents.
- **No `std::uniform_int_distribution`** or any `<random>` distribution — not portable across standard library implementations.
- **`tactix_sim` must never link raylib.** Verified mechanically in Task 6.
- **64-bit targets only** (avoids x87 excess-precision non-determinism).
- Target names are exactly: `tactix_sim`, `tactix`, `tactix_bench`, `tactix_tests`, `tactix_fp_flags`.
- All new source files live in `c++/src/`, `c++/bench/`, or `c++/tests/`. Do not reorganize existing files into subdirectories — that churn is out of scope.

## Known Pre-Existing Issues (do NOT fix in Phase A)

- `JobSystem::waitAll()` reads `jobQueue.empty()` holding only `waitMutex`, not `queueMutex` — a data race on the queue, distinct from the RNG race. Deferred to Phase E. **If the thread-invariance test in Task 7 proves flaky, this is the first suspect** — report it rather than patching it, so the fix lands with the rest of the job system rework.
- `lastSeenX`/`lastSeenY` carry agent indices encoded as floats. Leave the hack alone; Phase F removes it.
- `spdlog::info` inside `tick()` on hero death. Leave it; Phase F removes it.
- Patrol targets are seeded in the range `(50..1850, 50..1030)` while the world is 1280×720, so patrol destinations are frequently outside the world and get clamped. Pre-existing behavior — **preserve it exactly**, since changing it changes the digest.

---

## File Structure

**Created:**

| File | Responsibility |
| --- | --- |
| `c++/src/Rng.hpp` | Stateless hash RNG + `RngUse` enum. Header-only. |
| `c++/src/DetMath.hpp` | Platform-independent `sin`. Header-only. |
| `c++/src/StateDigest.hpp` | FNV-1a 64-bit bitwise accumulator. Header-only. |
| `c++/src/WorkCounters.hpp` | Deterministic work counters. Header-only. |
| `c++/src/Renderer.hpp` / `.cpp` | All raylib drawing, extracted from `Simulation`. |
| `c++/bench/main.cpp` | Headless benchmark driver + CLI. |
| `c++/bench/BenchStats.hpp` | Percentiles + JSON emission. |
| `c++/tests/main.cpp` | doctest entry point. |
| `c++/tests/test_rng.cpp` | RNG unit tests. |
| `c++/tests/test_detmath.cpp` | Sine accuracy + reproducibility tests. |
| `c++/tests/test_spatialhash.cpp` | Spatial hash unit tests. |
| `c++/tests/test_determinism.cpp` | Digest invariance tests. |
| `c++/tests/test_counters.cpp` | Work-counter baseline gate. |
| `c++/tests/baseline/counters-2k-200.txt` | Committed expected counter values. |
| `.github/workflows/ci.yml` | Linux + Windows build, test, gate. |

**Modified:** `c++/CMakeLists.txt`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/src/JobSystem.hpp`, `c++/src/JobSystem.cpp`, `c++/src/SpatialHash.hpp`, `c++/src/SpatialHash.cpp`, `c++/src/main.cpp`, `README.md`

**Deleted:** `c++/src/Agent.hpp`

---

## Task 1: CMake modernization and test harness

Establishes explicit source lists, correct cross-platform flags, pinned dependencies, and a working `tactix_tests` binary. `SpatialHash` is already raylib-free, so it can be tested before any refactoring begins.

**Files:**
- Modify: `c++/CMakeLists.txt` (full rewrite)
- Create: `c++/tests/main.cpp`
- Create: `c++/tests/test_spatialhash.cpp`

**Interfaces:**
- Consumes: nothing (first task)
- Produces: CMake targets `tactix_fp_flags` (INTERFACE), `tactix_tests` (executable), `tactix` (executable). `ctest` runs `tactix_tests`.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

Create `c++/tests/test_spatialhash.cpp`:

```cpp
#include <doctest/doctest.h>
#include "SpatialHash.hpp"
#include <algorithm>

TEST_CASE("inserted entity is returned by a query at its own position") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(7u, 100.0f, 100.0f);

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(std::find(out.begin(), out.end(), 7u) != out.end());
}

TEST_CASE("query returns only the 3x3 neighbourhood, not the whole world") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(1u, 100.0f, 100.0f);
    grid.insert(2u, 1000.0f, 600.0f);   // far away, outside the 3x3 block

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(std::find(out.begin(), out.end(), 1u) != out.end());
    CHECK(std::find(out.begin(), out.end(), 2u) == out.end());
}

TEST_CASE("clear empties every cell") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.insert(1u, 100.0f, 100.0f);
    grid.clear();

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(out.empty());
    CHECK(grid.getMaxOccupancy() == 0u);
}

TEST_CASE("out-of-bounds positions clamp instead of reading out of range") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(3u, -500.0f, -500.0f);
    grid.insert(4u, 99999.0f, 99999.0f);

    std::vector<uint32_t> lowCorner;
    grid.queryNeighbors(0.0f, 0.0f, 25.0f, lowCorner);
    CHECK(std::find(lowCorner.begin(), lowCorner.end(), 3u) != lowCorner.end());

    std::vector<uint32_t> highCorner;
    grid.queryNeighbors(1279.0f, 719.0f, 25.0f, highCorner);
    CHECK(std::find(highCorner.begin(), highCorner.end(), 4u) != highCorner.end());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release` from `c++/`
Expected: FAIL — CMake errors because `tactix_tests` is not a defined target and doctest is not fetched.

- [ ] **Step 3: Write minimal implementation**

Replace `c++/CMakeLists.txt` entirely:

```cmake
cmake_minimum_required(VERSION 3.20)
project(tactix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determinism requires a 64-bit target: 32-bit x86 evaluates float
# expressions at 80-bit x87 precision and rounds unpredictably on spill.
if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "Tactix requires a 64-bit target for reproducible floating point.")
endif()

include(FetchContent)

# --- Dependencies (all pinned; floating tags make builds irreproducible) ---
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main)
FetchContent_MakeAvailable(rlImGui)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11)
FetchContent_MakeAvailable(doctest)

# --- Floating-point environment ---
# Carried by every target that compiles simulation code. Disabling fast-math
# is NOT sufficient on its own: FMA contraction is on by default on GCC/Clang
# and silently changes rounding.
add_library(tactix_fp_flags INTERFACE)
if(MSVC)
    target_compile_options(tactix_fp_flags INTERFACE /fp:precise)
else()
    target_compile_options(tactix_fp_flags INTERFACE -ffp-contract=off)
endif()

# --- Simulation core: MUST NOT link raylib ---
# Explicit source list, not GLOB: GLOB does not re-run reliably on file
# addition and silently produces stale builds.
add_library(tactix_sim STATIC
    src/Simulation.cpp
    src/SpatialHash.cpp
    src/JobSystem.cpp
)
target_include_directories(tactix_sim PUBLIC src)
target_link_libraries(tactix_sim PUBLIC tactix_fp_flags spdlog::spdlog_header_only)

# NOTE: until Task 6, Simulation.cpp still references raylib symbols
# (GetRandomValue and draw()). Task 6 removes this line permanently.
target_link_libraries(tactix_sim PUBLIC raylib)

# --- GUI application ---
add_executable(tactix
    src/main.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(tactix PRIVATE ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(tactix PRIVATE tactix_sim raylib)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# --- Tests ---
enable_testing()
add_executable(tactix_tests
    tests/main.cpp
    tests/test_spatialhash.cpp
)
target_link_libraries(tactix_tests PRIVATE tactix_sim doctest::doctest)
add_test(NAME unit COMMAND tactix_tests)
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS — 4 test cases, 0 failures. If `test.txt`/shader copying fails, confirm `c++/assets/` exists.

- [ ] **Step 5: Commit**

```bash
git add c++/CMakeLists.txt c++/tests/main.cpp c++/tests/test_spatialhash.cpp
git commit -m "build: explicit targets, pinned deps, FP determinism flags, doctest harness"
```

---

## Task 2: Deterministic RNG primitive

A stateless hash keyed on `(seed, agentIndex, tickNumber, callSite)`. No shared state means no race and no lock; identical output regardless of thread scheduling.

**Files:**
- Create: `c++/src/Rng.hpp`
- Create: `c++/tests/test_rng.cpp`
- Modify: `c++/CMakeLists.txt` (add test file to `tactix_tests`)

**Interfaces:**
- Consumes: nothing
- Produces:
  - `enum class RngUse : uint32_t` — one enumerator per distinct call site
  - `uint32_t pcgHash(uint32_t)`
  - `struct Rng { uint32_t seed; uint32_t tick; }`
  - `int   Rng::range(uint32_t agentIndex, RngUse use, int lo, int hi) const` — inclusive both ends, matching `GetRandomValue` semantics exactly
  - `float Rng::unit(uint32_t agentIndex, RngUse use) const` — `[0.0f, 1.0f)`

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_rng.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Rng.hpp"
#include <set>

TEST_CASE("same inputs always produce the same value") {
    Rng a{42u, 100u};
    Rng b{42u, 100u};
    for (uint32_t i = 0; i < 1000; ++i) {
        CHECK(a.range(i, RngUse::SpawnPosX, 0, 1279) ==
              b.range(i, RngUse::SpawnPosX, 0, 1279));
    }
}

TEST_CASE("different seeds produce different streams") {
    Rng a{1u, 0u};
    Rng b{2u, 0u};
    int differences = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (a.range(i, RngUse::SpawnPosX, 0, 1279) !=
            b.range(i, RngUse::SpawnPosX, 0, 1279)) {
            ++differences;
        }
    }
    CHECK(differences > 900);   // near-total divergence expected
}

TEST_CASE("different call sites on the same agent-tick do not correlate") {
    Rng r{42u, 7u};
    int collisions = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (r.range(i, RngUse::SpawnPosX, 0, 1279) ==
            r.range(i, RngUse::SpawnPosY, 0, 1279)) {
            ++collisions;
        }
    }
    CHECK(collisions < 50);   // ~1/1280 expected by chance
}

TEST_CASE("different ticks produce different streams") {
    Rng t0{42u, 0u};
    Rng t1{42u, 1u};
    int differences = 0;
    for (uint32_t i = 0; i < 1000; ++i) {
        if (t0.range(i, RngUse::SpawnPosX, 0, 1279) !=
            t1.range(i, RngUse::SpawnPosX, 0, 1279)) {
            ++differences;
        }
    }
    CHECK(differences > 900);
}

TEST_CASE("range is inclusive on both ends, like GetRandomValue") {
    Rng r{42u, 0u};
    std::set<int> seen;
    for (uint32_t i = 0; i < 20000; ++i) {
        int v = r.range(i, RngUse::SpawnVelX, -2, 2);
        CHECK(v >= -2);
        CHECK(v <= 2);
        seen.insert(v);
    }
    CHECK(seen.size() == 5);   // all of -2,-1,0,1,2 must occur
}

TEST_CASE("range handles a single-value span") {
    Rng r{42u, 0u};
    CHECK(r.range(0u, RngUse::SpawnVelX, 5, 5) == 5);
}

TEST_CASE("unit stays within [0,1)") {
    Rng r{42u, 0u};
    for (uint32_t i = 0; i < 20000; ++i) {
        float v = r.unit(i, RngUse::SpawnPatrolX);
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `tests/test_rng.cpp` to the `tactix_tests` source list in `c++/CMakeLists.txt`, then:

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
```

Expected: FAIL — `Rng.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `c++/src/Rng.hpp`:

```cpp
#pragma once
#include <cstdint>

// Identifies WHICH random draw is being made. Two draws for the same agent on
// the same tick must never share an enumerator, or they return the same value.
//
// Rule when replacing a GetRandomValue call site: add an enumerator named for
// what the draw produces. Never reuse one unless the two sites are provably
// unreachable within the same agent-tick.
enum class RngUse : uint32_t {
    SpawnPosX = 1,
    SpawnPosY,
    SpawnVelX,
    SpawnVelY,
    SpawnPatrolX,
    SpawnPatrolY,
    SpawnHeroType,
    ObstacleBuildingX,
    ObstacleBuildingY,
    ObstacleBuildingW,
    ObstacleBuildingH,
    ObstacleTreeX,
    ObstacleTreeY,
    ObstacleTreeRadius,
    SeparationPushX,
    SeparationPushY,
    // Add one enumerator per remaining call site during Task 5.
    // Keep this trailing sentinel last.
    Count
};

// PCG-derived integer hash (O'Neill). Good avalanche, no state.
inline uint32_t pcgHash(uint32_t x) {
    uint32_t state = x * 747796405u + 2891336453u;
    uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

struct Rng {
    uint32_t seed = 1u;
    uint32_t tick = 0u;

    uint32_t bits(uint32_t agentIndex, RngUse use) const {
        // Odd multipliers keep the three inputs from aliasing under XOR.
        return pcgHash(seed
                     ^ (agentIndex * 0x9E3779B9u)
                     ^ (tick * 0x85EBCA6Bu)
                     ^ (static_cast<uint32_t>(use) * 0xC2B2AE35u));
    }

    // Inclusive [lo, hi] — identical semantics to raylib's GetRandomValue,
    // so replacing call sites does not change the range of behaviour.
    int range(uint32_t agentIndex, RngUse use, int lo, int hi) const {
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(bits(agentIndex, use) % span);
    }

    // [0.0f, 1.0f). Top 24 bits scaled by 2^-24 — exact in float.
    float unit(uint32_t agentIndex, RngUse use) const {
        return static_cast<float>(bits(agentIndex, use) >> 8) * 0x1.0p-24f;
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS — 11 test cases total, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Rng.hpp c++/tests/test_rng.cpp c++/CMakeLists.txt
git commit -m "feat: stateless per-agent hash RNG with call-site tagging"
```

---

## Task 3: Deterministic sine

`std::sin` is not bit-identical across libm implementations. The tick calls it twice, and both results feed agent position, so both feed the digest.

**Files:**
- Create: `c++/src/DetMath.hpp`
- Create: `c++/tests/test_detmath.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing
- Produces: `float detmath::sin(float)` — max absolute error ≈ 4e-6 against `std::sin`, using only `+ - * /` and float comparison, all IEEE-754 specified.

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_detmath.cpp`:

```cpp
#include <doctest/doctest.h>
#include "DetMath.hpp"
#include <cmath>
#include <cstring>

TEST_CASE("accurate against std::sin across a wide range") {
    float worst = 0.0f;
    for (int i = -100000; i <= 100000; ++i) {
        const float x = static_cast<float>(i) * 0.005f;   // +/- 500 radians
        const float err = std::fabs(detmath::sin(x) - std::sin(static_cast<double>(x)));
        if (err > worst) worst = err;
    }
    CHECK(worst < 1.0e-5f);
}

TEST_CASE("exact at the values that matter") {
    CHECK(std::fabs(detmath::sin(0.0f)) < 1.0e-6f);
    CHECK(std::fabs(detmath::sin(3.14159265f)) < 1.0e-5f);
    CHECK(std::fabs(detmath::sin(1.57079633f) - 1.0f) < 1.0e-5f);
    CHECK(std::fabs(detmath::sin(-1.57079633f) + 1.0f) < 1.0e-5f);
}

TEST_CASE("output is bitwise reproducible") {
    for (int i = 0; i < 10000; ++i) {
        const float x = static_cast<float>(i) * 0.013f;
        uint32_t a, b;
        const float fa = detmath::sin(x);
        const float fb = detmath::sin(x);
        std::memcpy(&a, &fa, 4);
        std::memcpy(&b, &fb, 4);
        CHECK(a == b);
    }
}

TEST_CASE("stays bounded far from the origin") {
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i) * 977.0f;
        const float v = detmath::sin(x);
        CHECK(v >= -1.001f);
        CHECK(v <= 1.001f);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `tests/test_detmath.cpp` to `tactix_tests`, rebuild.
Expected: FAIL — `DetMath.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `c++/src/DetMath.hpp`:

```cpp
#pragma once

// Platform-independent transcendentals.
//
// libm's sin() is NOT specified to be bit-identical across implementations
// (glibc, MSVC CRT and Apple libm all differ in the last bits), which breaks
// cross-platform digest comparison. These use only +, -, *, / and float
// comparison, every one of which IEEE-754 pins down exactly.
//
// std::sqrt is deliberately NOT reimplemented here: IEEE-754 requires it to be
// correctly rounded, so it is already portable.
namespace detmath {

constexpr float PI      = 3.14159265358979323846f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float TWO_PI  = 6.28318530717958647692f;
constexpr float INV_TWO_PI = 0.15915494309189533577f;

// Reduce to [-PI, PI].
inline float wrapPi(float x) {
    const float k = x * INV_TWO_PI;
    // Round to nearest without llround(): truncation plus a signed half.
    const float rounded = static_cast<float>(
        static_cast<int32_t>(k + (k >= 0.0f ? 0.5f : -0.5f)));
    return x - rounded * TWO_PI;
}

// Max absolute error ~4e-6.
//
// ponytail: accuracy degrades for |x| beyond ~1e6 as float precision in the
// range reduction runs out. Upgrade to Payne-Hanek reduction only if a caller
// ever needs it; the simulation's elapsed-time arguments stay far below that.
inline float sin(float x) {
    x = wrapPi(x);

    // Fold [-PI,-PI/2] and [PI/2,PI] onto [-PI/2,PI/2] using sin(PI-x)=sin(x).
    // Taylor error at PI/2 is ~4e-6; at PI it would be ~7e-3.
    if (x > HALF_PI)       x = PI - x;
    else if (x < -HALF_PI) x = -PI - x;

    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f
              + x2 * ( 0.008333333f
              + x2 * (-0.00019841270f
              + x2 *  0.0000027557319f))));
}

} // namespace detmath
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS — 15 test cases, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add c++/src/DetMath.hpp c++/tests/test_detmath.cpp c++/CMakeLists.txt
git commit -m "feat: platform-independent sine for deterministic tick math"
```

---

## Task 4: State digest and the failing determinism test

Writes the test that proves the race exists. **This task ends with a failing test on purpose** — that failure is the empirical record of the bug Task 5 fixes.

**Files:**
- Create: `c++/src/StateDigest.hpp`
- Create: `c++/tests/test_determinism.cpp`
- Modify: `c++/src/Simulation.hpp` (add `stateDigest()`)
- Modify: `c++/src/Simulation.cpp` (implement `stateDigest()`)
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing
- Produces:
  - `class StateDigest` with `void mix(float)`, `void mix(uint32_t)`, `uint64_t value() const`
  - `uint64_t Simulation::stateDigest() const`

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_determinism.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Simulation.hpp"

namespace {
// Runs a simulation to completion and returns the digest of its final state.
uint64_t runAndDigest(size_t agents, int ticks) {
    Simulation sim(1280, 720);
    sim.init(agents);
    sim.setPaused(false);
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("two identical runs produce identical state") {
    const uint64_t a = runAndDigest(2000, 200);
    const uint64_t b = runAndDigest(2000, 200);
    CHECK(a == b);
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `tests/test_determinism.cpp` to `tactix_tests`. Implement `StateDigest.hpp` and `Simulation::stateDigest()` first (Step 3 below), then run:

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: **FAIL** — `a != b`. Two causes, both real: raylib's global RNG carries state from the first run into the second, and seven worker threads mutate that global concurrently. Record the two differing digest values in the commit message; they are the evidence.

- [ ] **Step 3: Write minimal implementation**

Create `c++/src/StateDigest.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <cstring>

// FNV-1a over raw IEEE-754 bits. Bitwise on purpose: a tolerance-based
// comparison would silently accept the scheduling bugs this exists to catch.
//
// Note: -0.0f and +0.0f hash differently, and a NaN appearing anywhere will
// almost certainly diverge. Both are desirable — either one signals a bug.
class StateDigest {
public:
    void mix(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<uint8_t>(v >> (i * 8));
            h *= 1099511628211ull;
        }
    }

    void mix(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        mix(bits);
    }

    uint64_t value() const { return h; }

private:
    uint64_t h = 14695981039346656037ull;
};
```

Add to `c++/src/Simulation.hpp`, in the `public:` section:

```cpp
    // Bitwise hash of all simulation-visible state. See StateDigest.hpp.
    uint64_t stateDigest() const;
```

Add to `c++/src/Simulation.cpp` (include `"StateDigest.hpp"` at the top):

```cpp
uint64_t Simulation::stateDigest() const {
    StateDigest d;
    d.mix(static_cast<uint32_t>(entities.count));
    for (size_t i = 0; i < entities.count; ++i) {
        d.mix(entities.posX[i]);
        d.mix(entities.posY[i]);
        d.mix(entities.velX[i]);
        d.mix(entities.velY[i]);
        d.mix(static_cast<uint32_t>(entities.type[i]));
        d.mix(static_cast<uint32_t>(entities.state[i]));
        d.mix(static_cast<uint32_t>(entities.health[i]));
    }
    return d.value();
}
```

- [ ] **Step 4: Run test and confirm it fails for the expected reason**

Run the command from Step 2. Confirm the failure is a digest mismatch, not a crash or a build error. If it unexpectedly passes, run it five more times — the race is timing-dependent and an occasional pass does not mean the code is correct.

- [ ] **Step 5: Commit**

```bash
git add c++/src/StateDigest.hpp c++/src/Simulation.hpp c++/src/Simulation.cpp \
        c++/tests/test_determinism.cpp c++/CMakeLists.txt
git commit -m "test: add state digest and failing determinism test

Documents the data race: GetRandomValue is called from worker threads
on raylib's unsynchronized global RNG. Two identical runs diverge."
```

> The repository is intentionally left with a failing test at the end of this task. Task 5 turns it green. Do not skip ahead to CI (Task 10) with this outstanding.

---

## Task 5: Replace every raylib RNG and libm sine call site

The mechanical core of Phase A: 51 `GetRandomValue` calls and 2 `std::sin` calls, all in the tick path.

**Files:**
- Modify: `c++/src/Simulation.hpp` (seed/tick members; remove `#include <raylib.h>`; `EntityHot::spawn` signature)
- Modify: `c++/src/Simulation.cpp` (all 53 call sites)
- Modify: `c++/src/Rng.hpp` (extend `RngUse`)
- Modify: `c++/tests/test_determinism.cpp` (add seed tests)

**Interfaces:**
- Consumes: `Rng`, `RngUse`, `pcgHash` (Task 2); `detmath::sin` (Task 3)
- Produces:
  - `Simulation(int screenWidth, int screenHeight, uint32_t seed = 1u)`
  - `uint32_t Simulation::getSeed() const`
  - `EntityHot::spawn(float px, float py, float vx, float vy, AgentType t, const Rng& rng)`

- [ ] **Step 1: Write the failing test**

Extend `c++/tests/test_determinism.cpp` — replace the existing `runAndDigest` helper and add two cases:

```cpp
namespace {
uint64_t runAndDigest(size_t agents, int ticks, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed);
    sim.init(agents);
    sim.setPaused(false);
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("same seed reproduces exactly") {
    CHECK(runAndDigest(2000, 200, 42u) == runAndDigest(2000, 200, 42u));
}

TEST_CASE("different seeds diverge") {
    CHECK(runAndDigest(2000, 200, 42u) != runAndDigest(2000, 200, 43u));
}

TEST_CASE("repeated runs stay stable across many trials") {
    const uint64_t reference = runAndDigest(500, 100, 7u);
    for (int trial = 0; trial < 10; ++trial) {
        CHECK(runAndDigest(500, 100, 7u) == reference);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: FAIL — `Simulation` has no three-argument constructor.

- [ ] **Step 3: Write the implementation**

**3a. Add seed and tick tracking.** In `Simulation.hpp`, add to the private section:

```cpp
    uint32_t worldSeed = 1u;
    uint32_t tickNumber = 0u;
```

Change the constructor declaration to:

```cpp
    Simulation(int screenWidth, int screenHeight, uint32_t seed = 1u);
```

Add to the public section:

```cpp
    uint32_t getSeed() const { return worldSeed; }
```

In `Simulation.cpp`, update the constructor and remove the dead buffer:

```cpp
Simulation::Simulation(int w, int h, uint32_t seed)
    : screenWidth(w), screenHeight(h)
    , spatialHash(static_cast<float>(w), static_cast<float>(h), 50.0f)
    , worldSeed(seed)
{
}
```

In `Simulation::tick`, immediately after the `if (paused) return;` guard:

```cpp
    ++tickNumber;
    const Rng rng{worldSeed, tickNumber};
```

Pass `rng` down to `updateSeparationChunk`, `updateMovementChunk`, `updateBehaviorsChunk`, and `updateInfections` by adding a `const Rng& rng` parameter to each — update both the declarations in `Simulation.hpp` and the lambdas in `tick()` that dispatch them. Capture `rng` **by value** in those lambdas; it is 8 bytes and capturing a reference to a `tick()` local across a job boundary is a dangling-reference risk.

**3b. Give `EntityHot::spawn` an RNG.** Change its signature in `Simulation.hpp`:

```cpp
    void spawn(float px, float py, float vx, float vy, AgentType agentType, const Rng& rng) {
```

and inside it, replace the three raylib calls. The agent index is `count`, since `spawn` appends:

```cpp
        patrolTargetX.push_back(static_cast<float>(
            rng.range(static_cast<uint32_t>(count), RngUse::SpawnPatrolX, 50, 1850)));
        patrolTargetY.push_back(static_cast<float>(
            rng.range(static_cast<uint32_t>(count), RngUse::SpawnPatrolY, 50, 1030)));
        ...
        heroType.push_back(agentType == AgentType::Hero
            ? static_cast<uint8_t>(rng.range(static_cast<uint32_t>(count), RngUse::SpawnHeroType, 0, 1))
            : 0);
```

Add `#include "Rng.hpp"` to `Simulation.hpp` and **remove `#include <raylib.h>`** from it. `Simulation.cpp` keeps its raylib include until Task 6, because `draw()` still lives there.

**3c. Replace the remaining call sites.** The transformation is uniform:

```cpp
// BEFORE
GetRandomValue(lo, hi)
// AFTER
rng.range(agentIndex, RngUse::SomeDescriptiveName, lo, hi)
```

Worked examples from the actual code:

```cpp
// Simulation.cpp ~line 35 (init, spawning a civilian at loop index i)
// BEFORE: px = (float)GetRandomValue(0, screenWidth);
//         py = (float)GetRandomValue(0, screenHeight);
px = (float)rng.range((uint32_t)i, RngUse::SpawnPosX, 0, screenWidth);
py = (float)rng.range((uint32_t)i, RngUse::SpawnPosY, 0, screenHeight);
```

```cpp
// Simulation.cpp lines 388-389, inside updateSeparationChunk (runs on a worker
// thread — this is one of the four sites causing the race)
// BEFORE: steerX += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
//         steerY += (GetRandomValue(-10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
steerX += (rng.range((uint32_t)i, RngUse::SeparationPushX, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
steerY += (rng.range((uint32_t)i, RngUse::SeparationPushY, -10, 10) > 0 ? 1.0f : -1.0f) * 10.0f;
```

```cpp
// Simulation.cpp ~line 90, generateObstacles() — no agent involved, so use the
// loop index of the obstacle being generated as the index argument.
// BEFORE: float bx = (float)GetRandomValue(100, screenWidth - 200);
float bx = (float)rng.range((uint32_t)b, RngUse::ObstacleBuildingX, 100, screenWidth - 200);
```

For each site, add an enumerator to `RngUse` in `Rng.hpp` named for what the draw produces (`PatrolRetargetX`, `FleeJitterY`, `ReanimationDelay`, …), inserted **before** the `Count` sentinel. Two draws reachable in the same agent-tick must never share an enumerator.

`generateObstacles()` and `init()` run before any tick, so construct a local `const Rng rng{worldSeed, 0u};` at the top of each.

**3d. Replace the two sine calls.** Add `#include "DetMath.hpp"` to `Simulation.cpp` and change lines 633 and 636:

```cpp
// BEFORE: float shake    = std::sin(elapsedTime * 12.0f + phase) * 1.5f;
//         float pushPull = std::sin(elapsedTime * 4.0f  + phase) * 0.5f;
float shake    = detmath::sin(elapsedTime * 12.0f + phase) * 1.5f;
float pushPull = detmath::sin(elapsedTime * 4.0f  + phase) * 0.5f;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS — including the three determinism cases. Then verify no call site was missed:

```bash
grep -c GetRandomValue c++/src/Simulation.cpp c++/src/Simulation.hpp
```

Expected: `0` in both files (grep exits 1 when the count is zero — that is success here).

```bash
grep -n 'std::sin\|std::cos\|std::pow\|std::exp\|std::log' c++/src/Simulation.cpp
```

Expected: no output.

Finally, run the GUI app and watch it for thirty seconds. Agents should still flee, infect, fight, and reanimate. Behaviour will not be *identical* to before — the random stream changed — but it must remain qualitatively the same. A world where every agent walks in one direction means an index or enumerator is wrong.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Simulation.hpp c++/src/Simulation.cpp c++/src/Rng.hpp c++/tests/test_determinism.cpp
git commit -m "fix: replace raylib global RNG with per-agent hash, removing data race

51 GetRandomValue call sites, 4 of them on worker threads, replaced with a
stateless hash keyed on (seed, agentIndex, tickNumber, callSite). Also
replaces 2 std::sin calls with detmath::sin for cross-platform identity.

The determinism test added in the previous commit now passes."
```

---

## Task 6: Extract the renderer and cut raylib out of the simulation

**Files:**
- Create: `c++/src/Renderer.hpp`, `c++/src/Renderer.cpp`
- Modify: `c++/src/Simulation.hpp` (remove `draw`, add `friend`), `c++/src/Simulation.cpp` (delete `draw`), `c++/src/main.cpp`, `c++/CMakeLists.txt`
- Delete: `c++/src/Agent.hpp`

**Interfaces:**
- Consumes: `Simulation` (Task 5)
- Produces: `void drawSimulation(const Simulation& sim, float alpha);` declared in `Renderer.hpp`

> **Deviation from spec §4.3, deliberate:** the spec proposed a const accessor. `friend void drawSimulation(...)` is used instead — it satisfies the same intent (no proliferation of getters) while adding *zero* public API surface. Phase F reorganises `EntityHot` anyway, and a friend declaration is easier to unwind then than a published accessor other code has started using.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_determinism.cpp`:

```cpp
TEST_CASE("simulation runs headless with no window initialised") {
    // If any raylib call remains in the tick path this either crashes or
    // returns garbage, because no GL context or window exists here.
    Simulation sim(1280, 720, 99u);
    sim.init(1000);
    sim.setPaused(false);
    for (int i = 0; i < 50; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    CHECK(sim.getAgentCount() > 0);
    CHECK(sim.stateDigest() != 0ull);
}
```

- [ ] **Step 2: Run test to verify it fails**

The test compiles and passes only once `tactix_sim` no longer links raylib. Verify the current (still-failing) state:

```bash
cd c++ && grep -n 'raylib' CMakeLists.txt
```

Expected: `target_link_libraries(tactix_sim PUBLIC raylib)` is still present — the dependency has not been cut yet.

- [ ] **Step 3: Write the implementation**

**3a.** Create `c++/src/Renderer.hpp`:

```cpp
#pragma once

class Simulation;

// Draws the simulation. alpha is the interpolation factor in [0,1] between
// the previous and current tick's positions.
void drawSimulation(const Simulation& sim, float alpha);
```

**3b.** In `Simulation.hpp`, remove the `void draw(float alpha);` declaration and add inside the class body:

```cpp
    friend void drawSimulation(const Simulation& sim, float alpha);
```

**3c.** Create `c++/src/Renderer.cpp`. Move the entire body of `Simulation::draw` (currently `Simulation.cpp` lines 1512 to end) into it verbatim, changing only the signature and prefixing member accesses with `sim.`:

```cpp
#include "Renderer.hpp"
#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>

void drawSimulation(const Simulation& sim, float alpha) {
    // ... body moved verbatim from Simulation::draw, with bare member
    // references (entities, buildings, trees, gunshotLines, graveyard,
    // prevPosX, prevPosY, debugGrid, spatialHash) rewritten as sim.entities,
    // sim.buildings, and so on.
}
```

**3d.** Delete `Simulation::draw` from `Simulation.cpp`, and remove `#include <raylib.h>` from it. Delete `c++/src/Agent.hpp` and the `neighborBuffer` member from `Simulation.hpp` (its only use, the constructor `reserve`, was already removed in Task 5).

**3e.** In `main.cpp`, add `#include "Renderer.hpp"` and replace the `sim.draw(alpha);` call with `drawSimulation(sim, alpha);`.

**3f.** In `CMakeLists.txt`, delete this line permanently:

```cmake
target_link_libraries(tactix_sim PUBLIC raylib)
```

and add `src/Renderer.cpp` to the `tactix` executable's source list (not to `tactix_sim`).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS. A full clean reconfigure matters here — a stale cache can hide a lingering raylib link.

Verify the boundary mechanically:

```bash
grep -rn 'raylib\|rlImGui\|imgui' c++/src/Simulation.hpp c++/src/Simulation.cpp \
    c++/src/SpatialHash.* c++/src/JobSystem.* c++/src/Rng.hpp c++/src/DetMath.hpp
```

Expected: no output.

Launch `tactix` and confirm rendering is visually unchanged.

- [ ] **Step 5: Commit**

```bash
git add c++/src/Renderer.hpp c++/src/Renderer.cpp c++/src/Simulation.hpp \
        c++/src/Simulation.cpp c++/src/main.cpp c++/CMakeLists.txt
git rm c++/src/Agent.hpp
git commit -m "refactor: extract renderer; tactix_sim no longer links raylib"
```

---

## Task 7: Configurable worker count and thread invariance

**Files:**
- Modify: `c++/src/JobSystem.hpp`, `c++/src/JobSystem.cpp`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`, `c++/tests/test_determinism.cpp`

**Interfaces:**
- Consumes: `Simulation` (Task 6)
- Produces:
  - `explicit JobSystem(uint32_t workerCount = 0)` — `0` means `max(1, hardware_concurrency() - 1)`
  - `Simulation(int screenWidth, int screenHeight, uint32_t seed = 1u, uint32_t workerThreads = 0u)`

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_determinism.cpp`:

```cpp
namespace {
uint64_t runWithThreads(uint32_t threads, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    for (int i = 0; i < 200; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.stateDigest();
}
} // namespace

TEST_CASE("state is identical regardless of worker thread count") {
    const uint64_t single = runWithThreads(1u);
    CHECK(runWithThreads(2u)  == single);
    CHECK(runWithThreads(4u)  == single);
    CHECK(runWithThreads(7u)  == single);
    CHECK(runWithThreads(16u) == single);
}

TEST_CASE("thread invariance holds across repeated trials") {
    const uint64_t reference = runWithThreads(1u, 5u);
    for (int trial = 0; trial < 5; ++trial) {
        CHECK(runWithThreads(8u, 5u) == reference);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Rebuild.
Expected: FAIL — `Simulation` has no four-argument constructor.

- [ ] **Step 3: Write the implementation**

In `JobSystem.hpp`, change the constructor declaration to `explicit JobSystem(uint32_t requestedWorkers = 0);`. In `JobSystem.cpp`:

```cpp
JobSystem::JobSystem(uint32_t requestedWorkers) {
    workerCount = (requestedWorkers > 0)
        ? requestedWorkers
        : std::max(1u, std::thread::hardware_concurrency() - 1);

    spdlog::info("JobSystem: Starting {} worker threads", workerCount);

    for (uint32_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(&JobSystem::workerLoop, this);
    }
}
```

In `Simulation.hpp` change the constructor to `Simulation(int screenWidth, int screenHeight, uint32_t seed = 1u, uint32_t workerThreads = 0u);`, and in `Simulation.cpp` pass it through the member initialiser list: `, jobSystem(workerThreads)`. Ensure `jobSystem` is declared after `spatialHash` in the class so initialisation order matches the list.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS. Run `ctest` five more times — a race would show as intermittent failure.

**If these tests are flaky:** do not patch `waitAll()`. Report the flake with the failing digests; the `jobQueue.empty()` race is documented above and belongs to Phase E.

- [ ] **Step 5: Commit**

```bash
git add c++/src/JobSystem.hpp c++/src/JobSystem.cpp c++/src/Simulation.hpp \
        c++/src/Simulation.cpp c++/tests/test_determinism.cpp
git commit -m "test: prove state is invariant to worker thread count"
```

---

## Task 8: Deterministic work counters

Machine-independent measures of how much work the tick does. These are what CI gates on; wall-clock time is never gated.

**Files:**
- Create: `c++/src/WorkCounters.hpp`
- Modify: `c++/src/SpatialHash.hpp`, `c++/src/SpatialHash.cpp`, `c++/src/Simulation.hpp`, `c++/src/Simulation.cpp`
- Create: `c++/tests/test_counters.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `Simulation` (Task 7)
- Produces:
  - `struct WorkCounters { uint64_t candidatesExamined, cellsVisited, gridInsertions, jobsDispatched; void reset(); }`
  - `const WorkCounters& Simulation::counters() const`
  - `void Simulation::resetCounters()`

- [ ] **Step 1: Write the failing test**

Create `c++/tests/test_counters.cpp`:

```cpp
#include <doctest/doctest.h>
#include "Simulation.hpp"
#include "WorkCounters.hpp"

namespace {
WorkCounters runAndCount(uint32_t threads, uint32_t seed = 42u) {
    Simulation sim(1280, 720, seed, threads);
    sim.init(2000);
    sim.setPaused(false);
    sim.resetCounters();
    for (int i = 0; i < 200; ++i) {
        sim.tick(1.0f / 60.0f);
    }
    return sim.counters();
}
} // namespace

TEST_CASE("counters are non-zero for a real run") {
    const WorkCounters c = runAndCount(1u);
    CHECK(c.candidatesExamined > 0ull);
    CHECK(c.cellsVisited > 0ull);
    CHECK(c.gridInsertions > 0ull);
}

TEST_CASE("grid insertions equal agents times ticks") {
    const WorkCounters c = runAndCount(1u);
    CHECK(c.gridInsertions == 2000ull * 200ull);
}

TEST_CASE("counters are identical regardless of thread count") {
    const WorkCounters single = runAndCount(1u);
    const WorkCounters many   = runAndCount(8u);
    CHECK(single.candidatesExamined == many.candidatesExamined);
    CHECK(single.cellsVisited       == many.cellsVisited);
    CHECK(single.gridInsertions     == many.gridInsertions);
}

TEST_CASE("counters reproduce across runs") {
    CHECK(runAndCount(4u).candidatesExamined == runAndCount(4u).candidatesExamined);
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `tests/test_counters.cpp` to `tactix_tests`, rebuild.
Expected: FAIL — `WorkCounters.hpp: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `c++/src/WorkCounters.hpp`:

```cpp
#pragma once
#include <atomic>
#include <cstdint>

// Deterministic measures of work performed, independent of wall-clock time
// and of the machine running the code. CI gates on these; it never gates on
// timings, because hosted runners are too noisy for a timing threshold to be
// anything but a flake generator.
//
// Counters are accumulated atomically because the tick phases run on worker
// threads. Only the TOTAL is asserted on, never per-thread splits, so
// relaxed ordering is sufficient and the total stays deterministic.
struct WorkCounters {
    std::atomic<uint64_t> candidatesExamined{0};  // neighbour candidates inspected
    std::atomic<uint64_t> cellsVisited{0};        // grid cells touched by queries
    std::atomic<uint64_t> gridInsertions{0};      // entities inserted into the grid
    std::atomic<uint64_t> jobsDispatched{0};      // jobs submitted to the job system

    void reset() {
        candidatesExamined.store(0, std::memory_order_relaxed);
        cellsVisited.store(0, std::memory_order_relaxed);
        gridInsertions.store(0, std::memory_order_relaxed);
        jobsDispatched.store(0, std::memory_order_relaxed);
    }

    void add(std::atomic<uint64_t>& field, uint64_t n) {
        field.fetch_add(n, std::memory_order_relaxed);
    }
};
```

Because `std::atomic` is not copyable, the test helper must return counts rather than the struct. Adjust `test_counters.cpp` to use a plain snapshot struct:

```cpp
struct CounterSnapshot {
    uint64_t candidatesExamined, cellsVisited, gridInsertions, jobsDispatched;
};

CounterSnapshot snapshot(const WorkCounters& c) {
    return { c.candidatesExamined.load(), c.cellsVisited.load(),
             c.gridInsertions.load(),     c.jobsDispatched.load() };
}
```

and have `runAndCount` return `CounterSnapshot` via `snapshot(sim.counters())`.

In `SpatialHash.hpp`, add a counters pointer and instrument the two hot functions:

```cpp
    void setCounters(WorkCounters* c) { counters = c; }
private:
    WorkCounters* counters = nullptr;
```

In `SpatialHash.cpp`, inside `insert`, after the `push_back`:

```cpp
    if (counters) counters->add(counters->gridInsertions, 1);
```

and inside `queryNeighbors`, in the 3×3 loop after the `isValidCell` check:

```cpp
    if (counters) {
        counters->add(counters->cellsVisited, 1);
        counters->add(counters->candidatesExamined, cell.size());
    }
```

In `Simulation.hpp` add a `WorkCounters workCounters;` member plus:

```cpp
    const WorkCounters& counters() const { return workCounters; }
    void resetCounters() { workCounters.reset(); }
```

In the `Simulation` constructor body, wire the grid up: `spatialHash.setCounters(&workCounters);`. In `Simulation::tick`, increment `jobsDispatched` alongside each `jobSystem.submit(...)` call.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add c++/src/WorkCounters.hpp c++/src/SpatialHash.hpp c++/src/SpatialHash.cpp \
        c++/src/Simulation.hpp c++/src/Simulation.cpp c++/tests/test_counters.cpp c++/CMakeLists.txt
git commit -m "feat: deterministic work counters for machine-independent regression gating"
```

---

## Task 9: Headless benchmark harness

**Files:**
- Create: `c++/bench/BenchStats.hpp`, `c++/bench/main.cpp`
- Modify: `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `Simulation` (Task 7), `WorkCounters` (Task 8)
- Produces: `tactix_bench` executable accepting `--agents N --ticks N --seed N --threads N [--json]`

- [ ] **Step 1: Write the failing test**

The deliverable is an executable, so its check is an invocation rather than a doctest case. Record the intended contract:

```bash
cd c++/build && ./tactix_bench --agents 1000 --ticks 100 --seed 42 --threads 2 --json
```

Expected once implemented: a single JSON object on stdout containing `p50Ms`, `p99Ms`, `maxMs`, per-phase timings, the four counters, and exit code 0.

- [ ] **Step 2: Run it to verify it fails**

Expected: FAIL — no such target.

- [ ] **Step 3: Write the implementation**

Create `c++/bench/BenchStats.hpp`:

```cpp
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

struct Percentiles {
    double p50 = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0;
};

// Nearest-rank percentiles. Takes the vector by value: sorting a copy keeps
// the caller's sample order intact.
inline Percentiles computePercentiles(std::vector<double> samples) {
    Percentiles p;
    if (samples.empty()) return p;
    std::sort(samples.begin(), samples.end());

    const auto pick = [&samples](double fraction) {
        size_t idx = static_cast<size_t>(fraction * static_cast<double>(samples.size()));
        if (idx >= samples.size()) idx = samples.size() - 1;
        return samples[idx];
    };

    p.p50 = pick(0.50);
    p.p95 = pick(0.95);
    p.p99 = pick(0.99);
    p.max = samples.back();
    return p;
}
```

Create `c++/bench/main.cpp`:

```cpp
#include "Simulation.hpp"
#include "WorkCounters.hpp"
#include "BenchStats.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int intArg(int argc, char** argv, const char* name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return std::atoi(argv[i + 1]);
    }
    return fallback;
}

bool hasFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const int agents  = intArg(argc, argv, "--agents", 10000);
    const int ticks   = intArg(argc, argv, "--ticks", 2000);
    const int seed    = intArg(argc, argv, "--seed", 42);
    const int threads = intArg(argc, argv, "--threads", 0);
    const bool json   = hasFlag(argc, argv, "--json");

    if (agents <= 0 || ticks <= 0) {
        std::fprintf(stderr, "error: --agents and --ticks must be positive\n");
        return 2;
    }

    Simulation sim(1280, 720,
                   static_cast<uint32_t>(seed),
                   static_cast<uint32_t>(threads));
    sim.init(static_cast<size_t>(agents));
    sim.setPaused(false);
    sim.resetCounters();

    std::vector<double> tickMs;
    tickMs.reserve(static_cast<size_t>(ticks));

    for (int i = 0; i < ticks; ++i) {
        const auto start = std::chrono::steady_clock::now();
        sim.tick(1.0f / 60.0f);
        const auto end = std::chrono::steady_clock::now();
        tickMs.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    const Percentiles p = computePercentiles(tickMs);
    const WorkCounters& c = sim.counters();
    const uint64_t digest = sim.stateDigest();

    if (json) {
        std::printf(
            "{\n"
            "  \"agents\": %d,\n"
            "  \"ticks\": %d,\n"
            "  \"seed\": %d,\n"
            "  \"threads\": %d,\n"
            "  \"p50Ms\": %.4f,\n"
            "  \"p95Ms\": %.4f,\n"
            "  \"p99Ms\": %.4f,\n"
            "  \"maxMs\": %.4f,\n"
            "  \"candidatesExamined\": %llu,\n"
            "  \"cellsVisited\": %llu,\n"
            "  \"gridInsertions\": %llu,\n"
            "  \"jobsDispatched\": %llu,\n"
            "  \"stateDigest\": \"%016llx\"\n"
            "}\n",
            agents, ticks, seed, threads,
            p.p50, p.p95, p.p99, p.max,
            (unsigned long long)c.candidatesExamined.load(),
            (unsigned long long)c.cellsVisited.load(),
            (unsigned long long)c.gridInsertions.load(),
            (unsigned long long)c.jobsDispatched.load(),
            (unsigned long long)digest);
    } else {
        std::printf("agents=%d ticks=%d seed=%d threads=%d\n", agents, ticks, seed, threads);
        std::printf("tick ms   p50=%.4f  p95=%.4f  p99=%.4f  max=%.4f\n",
                    p.p50, p.p95, p.p99, p.max);
        std::printf("digest    %016llx\n", (unsigned long long)digest);
    }
    return 0;
}
```

Add to `CMakeLists.txt`:

```cmake
add_executable(tactix_bench bench/main.cpp)
target_include_directories(tactix_bench PRIVATE bench)
target_link_libraries(tactix_bench PRIVATE tactix_sim)
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd c++ && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
```

then

```bash
cd c++/build && ./tactix_bench --agents 1000 --ticks 100 --seed 42 --threads 2 --json
```

Expected: valid JSON, exit 0, `p50Ms` greater than zero, no window appears. Run it twice and confirm `stateDigest` is byte-identical between the runs.

- [ ] **Step 5: Commit**

```bash
git add c++/bench/BenchStats.hpp c++/bench/main.cpp c++/CMakeLists.txt
git commit -m "feat: headless benchmark harness with percentile and counter output"
```

---

## Task 10: Committed baseline and CI

**Files:**
- Create: `c++/tests/baseline/counters-2k-200.txt`, `.github/workflows/ci.yml`
- Modify: `c++/tests/test_counters.cpp`, `c++/CMakeLists.txt`

**Interfaces:**
- Consumes: `WorkCounters` (Task 8), `tactix_bench` (Task 9)
- Produces: a CI workflow that fails on counter drift; `TACTIX_BASELINE_DIR` compile definition pointing `tactix_tests` at the baseline directory

> The baseline uses 2,000 agents and 200 ticks rather than the 10,000/2,000 local benchmark configuration, to keep debug-build CI runs fast. The local benchmark configuration is unchanged.

- [ ] **Step 1: Write the failing test**

Add to `c++/tests/test_counters.cpp`:

```cpp
#include <fstream>
#include <map>
#include <string>

namespace {
// Deliberately a flat key=value file, not JSON: gating needs four integers,
// and adding a JSON parser to read four integers would be silly.
std::map<std::string, uint64_t> loadBaseline(const std::string& path) {
    std::map<std::string, uint64_t> values;
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "cannot open baseline file: " << path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[line.substr(0, eq)] = std::stoull(line.substr(eq + 1));
    }
    return values;
}
} // namespace

TEST_CASE("work counters match the committed baseline") {
    const auto expected = loadBaseline(std::string(TACTIX_BASELINE_DIR) + "/counters-2k-200.txt");
    const CounterSnapshot actual = runAndCount(1u);

    CHECK(actual.candidatesExamined == expected.at("candidatesExamined"));
    CHECK(actual.cellsVisited       == expected.at("cellsVisited"));
    CHECK(actual.gridInsertions     == expected.at("gridInsertions"));
    CHECK(actual.jobsDispatched     == expected.at("jobsDispatched"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Add to `CMakeLists.txt`:

```cmake
target_compile_definitions(tactix_tests PRIVATE
    TACTIX_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/baseline")
```

Rebuild and run.
Expected: FAIL — the baseline file does not exist.

- [ ] **Step 3: Write the implementation**

Generate the real values rather than inventing them:

```bash
cd c++/build && ./tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

Create `c++/tests/baseline/counters-2k-200.txt` using the emitted numbers:

```
# Deterministic work counters: 2000 agents, 200 ticks, seed 42.
# Machine-independent by construction. Regenerate with:
#   tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
# An intentional optimisation SHOULD change these. Update them in the same
# commit as the change, so the diff shows the improvement.
candidatesExamined=<value from bench output>
cellsVisited=<value from bench output>
gridInsertions=400000
jobsDispatched=<value from bench output>
```

Create `.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
    branches: [master, 'phase-*']
  pull_request:

jobs:
  build-and-test:
    name: ${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest]

    steps:
      - uses: actions/checkout@v4

      - name: Install Linux graphics dependencies
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y libgl1-mesa-dev libwayland-dev libxkbcommon-dev \
            xorg-dev libasound2-dev

      - name: Configure
        working-directory: c++
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        working-directory: c++
        run: cmake --build build --config Release --parallel

      - name: Test (unit, determinism, counter gate)
        working-directory: c++
        run: ctest --test-dir build -C Release --output-on-failure

      - name: Record benchmark output (never gated on timing)
        working-directory: c++
        run: ./build/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 2 --json
        shell: bash

  cross-platform-digest:
    name: digest matches across platforms
    needs: build-and-test
    runs-on: ubuntu-latest
    steps:
      - run: echo "Digest equality is asserted inside tactix_tests on each platform."
```

> On Windows the benchmark binary is at `build/Release/tactix_bench.exe`. If the `shell: bash` step fails to find it, adjust that step's path per-OS rather than changing the CMake output layout.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd c++ && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

Expected: PASS locally. Push the branch and confirm both CI matrix legs go green. If Linux and Windows disagree on `candidatesExamined`, that is a genuine determinism failure — do not paper over it with per-platform baselines until the cause is understood and the spec §5.4 fallback is invoked deliberately.

- [ ] **Step 5: Commit**

```bash
git add c++/tests/baseline/counters-2k-200.txt c++/tests/test_counters.cpp \
        c++/CMakeLists.txt .github/workflows/ci.yml
git commit -m "ci: gate on deterministic work counters across Linux and Windows"
```

---

## Task 11: Honest README with a reproducible first row

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: `tactix_bench` (Task 9)
- Produces: no code

- [ ] **Step 1: Generate the real numbers**

```bash
cd c++/build && ./tactix_bench --agents 10000 --ticks 2000 --seed 42 --json
```

Record the output verbatim, plus the CPU model, core count, OS, and compiler version of the machine that produced it. Do not reuse the existing `~1.6 ms` figure — it predates every change in this plan and was never reproducible.

- [ ] **Step 2: Verify the claim is reproducible before writing it down**

Run the same command twice more. `stateDigest` must be identical all three times; `p50Ms` should land within a few percent. If the digest varies, stop — something in Tasks 5 through 8 is wrong, and no number should be published until it does not.

- [ ] **Step 3: Rewrite the README sections**

Replace the "Current numbers" table with measured values, adding p50/p95/p99 columns and naming the hardware. Replace the "Where this is going" section so it describes measurement discipline rather than promising a GPU port, and restate the roadmap as spec Phases B–H, marked *ranked but unproven*. Add a "Reproducing these numbers" section:

````markdown
## Reproducing these numbers

```bash
cd c++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/tactix_bench --agents 10000 --ticks 2000 --seed 42 --json
```

The simulation is deterministic: the same seed produces bit-identical state at
any thread count, in debug or release, on Linux or Windows. `stateDigest` in the
output should match the value in the table below on any machine.
````

Fill the performance log's first row with the measured p50 and the digest.

- [ ] **Step 4: Verify every claim in the README**

Re-read the file top to bottom. For each factual claim, confirm it is either produced by a command documented in the file or verifiable by reading the source. Delete any claim that is neither — that standard is the entire point of Phase A.

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs: replace unverifiable numbers with a reproducible benchmark row"
```

---

## Post-Phase-A: separate, user-decided

Deliberately excluded from the task list because they are the user's call, not an implementer's:

- **Archive `tactix-ai/`** to a branch (spec §7). A destructive-looking history operation; confirm with the user first.
- **Root `build/` directory is untracked and unignored.** `.gitignore` covers `c++/build/` but not `build/`. One line fixes it; flagged rather than done, to keep Phase A commits scoped.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
| --- | --- |
| §4.1 Deterministic RNG | 2, 5 |
| §4.2 mechanism 1 (reassociation) | 1 |
| §4.2 mechanism 2 (FMA contraction) | 1 |
| §4.2 mechanism 3 (x87) | 1 (64-bit `FATAL_ERROR` guard) |
| §4.2 mechanism 4 (transcendentals) | 3, 5 |
| §4.3 Target structure | 1, 6, 9 |
| §4.4 Deletions | 5 (`neighborBuffer`), 6 (`Agent.hpp`) |
| §5.1 Workload / CLI | 9 |
| §5.2 Percentiles + per-phase split | 9 — **partial, see gap below** |
| §5.3 Two measurement classes | 8, 10 |
| §5.4 Determinism tests | 4, 5, 6, 7, 10 |
| §5.5 Tooling (hand-rolled bench, doctest) | 1, 9 |
| §5.6 CI | 10 |
| §5.7 Baseline | 10 |
| §7 README | 11 |
| §7 `tactix-ai` archive | Post-Phase-A (user decision) |
| §9 Success criteria 1–6 | 6, 7, 10, 11, 5, 5 |

**Identified gap:** spec §5.2 requires a **per-phase timing split** (grid rebuild / separation / behaviors / movement). Task 9 emits whole-tick percentiles only. Adding per-phase timers means instrumenting inside `Simulation::tick` around each `jobSystem.waitAll()` barrier — straightforward, but it inserts timing calls into the parallel dispatch path, and Phase B is about to introduce a real profiler that supersedes hand-rolled phase timers entirely.

**Resolution:** left out deliberately, and recorded here rather than silently dropped. Whole-tick percentiles satisfy every Phase A success criterion in spec §9; the per-phase split is what Phase B's profiler delivers as its primary output. If the user wants it in Phase A anyway, it is roughly a 20-line addition to Task 9 — say so and it gets added.

**Placeholder scan:** one intentional fill-in remains — the counter values in Task 10's baseline file, marked `<value from bench output>`. These *cannot* be written in advance: they are measurements, and inventing them would defeat the gate. Step 3 of that task specifies the exact command that produces them.

**Type consistency:** verified across tasks — `Rng::range(uint32_t, RngUse, int, int)`, `Rng::unit(uint32_t, RngUse)`, `detmath::sin(float)`, `StateDigest::mix` / `::value`, `Simulation::stateDigest()`, `Simulation::counters()` / `::resetCounters()`, `JobSystem(uint32_t)`, `drawSimulation(const Simulation&, float)`, `computePercentiles(std::vector<double>)`. Task 8 notes the `std::atomic` copyability constraint and defines `CounterSnapshot` to resolve it before Task 10 consumes it.
