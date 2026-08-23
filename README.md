# Tactix

Data-oriented agent simulation in C++. Ten thousand autonomous agents at 60 ticks per second.

The point of the project is the performance work rather than the scenario. Every feature exists
to put pressure on memory layout, spatial queries, or parallelism. Phase A of the project's
[performance-foundation design spec](docs/superpowers/specs/2026-08-19-tactix-performance-foundation-design.md)
replaced an unverifiable `~1.6 ms` claim with a deterministic simulation, a headless benchmark
harness, and a CI gate — the numbers below are what that harness actually measured, and the
"Reproducing these numbers" section is the command that produced them.

![Ten thousand agents at the fitted zoom: two armies engaged in the centre, a volley of 625 arrows
in flight above them, archer reserves still formed up on both flanks, and the blood left where the
lines have already met.](docs/images/field.png)

## Current numbers

Measured with `tactix_bench --agents 10000 --ticks 2000 --seed 42 --json` (Release build), on:

- AMD Ryzen 7 7700X (8 physical cores / 16 logical), Windows 11 Pro 10.0.22621, MSVC 19.29.30153.0,
  Release build
- 15 worker threads (`hardware_concurrency() - 1`)

| Metric | Value |
| --- | --- |
| Agents | 10,000 |
| Simulation rate | 60 ticks/sec, fixed timestep |
| Tick cost, p50 | 8.6383 ms |
| Tick cost, p95 | 10.8953 ms |
| Tick cost, p99 | 11.7172 ms |
| Tick cost, max | 14.9782 ms |
| Worker threads | 15 (this machine); thread count does not change simulation state (see below) |
| Agent state | structure of arrays (see `SoldierHot` in [`c++/src/Simulation.hpp`](c++/src/Simulation.hpp)) |
| Neighbor query | uniform grid hash, 3x3 cell lookup |
| State digest (seed 42) | `5b01c0c4e1e73d6b` |

These are **down** from a previously published p50 of 9.0937 ms, across a body of work that added
armor, shields, five formations and a legion line relief. That direction was not the prediction, so
it is worth saying exactly where it comes from rather than banking it.

Two runs, same build, same seed. As shipped: **8.6383 ms**. With every `kWoundChancePct` entry and
all shield cover forced back to the old lethality (melee always wounds, arrows land 45 percent of
the time) while every new code path stays live: **8.4975 ms**.

Those two numbers separate the two effects:

- **The new code costs about 0.14 ms.** That is the gap between the two runs, and it is what armor
  and shields buy with longer-lived soldiers: more men alive means more men queried.
- **The drop from 9.09 ms is pacing, not speed.** Run 2 keeps every new branch, table lookup and
  extra roll and still comes in 0.6 ms under the old figure, so the new code is not what made the
  tick cheaper. Formations are: a shieldwall marches at 0.60 of its unit's speed and a phalanx at
  0.50, so after a fixed 2000 ticks the battle is simply less far advanced, with fewer men locked
  into the melee that dominates the tick.

This is the same attribution method the `kArrowHitChancePct` note below uses, pointed the other way.
Cost per live agent did not improve; the run reaches a cheaper part of the battle. A longer run
would close the gap, and the honest reading is that this work is roughly performance-neutral rather
than a 5 percent win.

The `max` figure sits inside the 16.67 ms a 60 Hz frame allows with about 1.7 ms of headroom at
10,000 agents. p99 is comfortable; it is the tail that is tight.

The previously published 9.0937 ms had one named cause of its own: soldiers collide with each other.
`phaseContact` ([`c++/src/Contact.cpp`](c++/src/Contact.cpp)) runs one neighbor query per soldier per
tick to push overlapping bodies apart, which roughly doubles the tick's neighbor-query load. Measured
directly on that build, replacing its `queryNeighbors` call with an empty candidate set gave a p50 of
6.0019 ms, so the non-penetration pass accounted for about 3.1 ms of the 9.09 ms figure.

That is the same attribution method the earlier `kArrowHitChancePct` note used, and it is what makes
the claim checkable instead of plausible.

The earlier doubling from p50 3.3468 ms to 6.6272 ms was also gameplay rather than regression:
arrows used to wound on every contact and now roll against `kArrowHitChancePct`
([`c++/src/Units.hpp`](c++/src/Units.hpp)), so armies survive far longer and a 2000-tick run spends
most of its ticks simulating a nearly full field instead of the handful of survivors left after an
early massacre. Setting that constant back to 100 on that build gave a p50 of 2.8613 ms, below the
figure before it. Cost per live agent went down; the number of live agents went up.

**This tick cost includes work-counter instrumentation overhead.** `tactix_bench` increments seven
`std::atomic` counters (`WorkCounters`, see [`c++/src/WorkCounters.hpp`](c++/src/WorkCounters.hpp))
several hundred thousand times per tick from worker threads, and the counters share a cache line.
That contention is real and is baked into every number above; it was deliberately left in place because
the proper fix — giving each worker its own cache-line-sized counter block — reshapes a call
signature that Phase F rewrites wholesale anyway. Phase B's profiler will quantify exactly how much
of the tick this accounts for.

There is no render-rate or frame-timing claim here: `tactix_bench` is headless and never opens a
window. The interactive `tactix` binary still renders through raylib/ImGui (see
[`c++/src/Renderer.cpp`](c++/src/Renderer.cpp)), but nothing about its frame rate has been measured
by a reproducible command, so no number for it appears here.

Full feature breakdown and phase history in [`c++/README.md`](c++/README.md). Architecture and
performance budgets in [`c++/docs/Design Document.md`](c++/docs/Design%20Document.md).

## Reproducing these numbers

```bash
cd c++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/tactix_bench --agents 10000 --ticks 2000 --seed 42 --json
```

(On Linux/macOS the binary lands at `./build/tactix_bench` instead of `./build/Release/tactix_bench`.)

The simulation is deterministic across worker thread counts: the same seed produces bit-identical
state — the `stateDigest` field above — no matter how many worker threads run it. That property is
enforced by [`c++/tests/test_determinism.cpp`](c++/tests/test_determinism.cpp) — including the
`"state is identical regardless of worker thread count"` and
`"thread invariance holds across repeated trials"` cases — and gated in CI by
[`.github/workflows/ci.yml`](.github/workflows/ci.yml). Run the command above three times: the
`stateDigest` should not change; `p50Ms` should vary only by normal scheduling noise. That was
verified on the machine above before this row was published: three consecutive runs all reported
`stateDigest = c68dedbbad082126`, with `p50Ms` of 3.2226, 3.3559, and 3.3468 (the table above uses
the third run).

**Debug/Release identity is a separate, weaker claim: verified locally, not gated in CI.**
`.github/workflows/ci.yml` only ever builds `-DCMAKE_BUILD_TYPE=Release`, and no test in
`c++/tests/` compares a Debug digest to a Release digest — so unlike the thread-count property
above, this one has no automated regression protection. What is true: the same seed was checked to
produce the same `stateDigest` in a Debug build as in Release, at the baseline configuration
(`--agents 2000 --ticks 200 --seed 42 --threads 1`, the configuration
[`c++/tests/baseline/counters-2k-200.txt`](c++/tests/baseline/counters-2k-200.txt) uses) — not at
the 10,000-agent configuration this page publishes numbers for.

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
./build-debug/Debug/tactix_bench --agents 2000 --ticks 200 --seed 42 --threads 1 --json
```

That produced `stateDigest = 6ddc96530e96e9d4` in Debug, matching both Release at the same
configuration and the value committed in `counters-2k-200.txt`. This is expected by construction —
the floating-point flags in `c++/CMakeLists.txt` (`/fp:precise` on MSVC, `-ffp-contract=off`
elsewhere) apply to `tactix_sim` unconditionally, not per build type — but that is the mechanism,
not the evidence. The command above is the evidence, and it has been run at one configuration on
one machine, not continuously gated.

**A caveat about that CI gate:** [`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds and
tests on both `ubuntu-latest` and `windows-latest`, but as of this writing it has not been pushed to
a remote and observed running there. The counter-baseline gate it runs was proven to bite locally —
the test in `c++/tests/test_counters.cpp` was written first, confirmed to fail with no baseline
present, then made to pass against a committed baseline (see Task 10 of the
[implementation plan](docs/superpowers/plans/2026-08-19-phase-a-performance-foundation.md)) — and
`ctest --test-dir build -C Release` passes on this Windows machine. But the Linux leg specifically
is unverified. Nothing here should be read as "CI is green on Linux" until it has actually run
there.

## Where this is going

Phase A's job was measurement discipline, not speed: a deterministic simulation, a benchmark that
reports percentiles instead of an eyeballed overlay number, and a CI gate that fails a build if a
change breaks reproducibility. That discipline — no claim without a command that reproduces it —
is what carries forward, not a specific performance target.

The table below is the plan's roadmap for Phases B through H, from §6 ("Phase sequencing") of the
[design spec](docs/superpowers/specs/2026-08-19-tactix-performance-foundation-design.md).
**It is ranked but unproven.** The order reflects expectation, not a conclusion — nothing past
Phase A has been measured yet. Phase B's profiler is explicitly the thing that gets to override
this ordering: if a real tick breakdown says something else is the bottleneck, the profile wins,
not this table. In particular, this is not a commitment to a GPU port (Phase H) on any timeline —
Phase H is only attempted after the CPU-side phases are measured and ordered, and none of them
have started.

### Roadmap (Phases B–H, ranked but unproven)

| Phase | Goal |
| --- | --- |
| A | Foundation: deterministic sim, headless benchmark, CI gate. **Complete — this README's numbers are its output.** |
| B | Integrate a profiler; publish the actual tick breakdown; name the top three costs with evidence. |
| C | Flat CSR spatial grid (counts → prefix sum → dense payload); query returns an index range instead of copying. |
| D | Reorder agent arrays by cell (or Morton order) each grid rebuild, so neighbor access is contiguous. |
| E | Replace the `std::function` + single-mutex job queue with an atomic-index `parallel_for`. |
| F | Hot/cold split of agent state; decompose `updateBehaviorsChunk`; remove the remaining serial scan and hot-path logging. |
| G | SIMD separation kernel, constrained to preserve floating-point summation order. |
| H | GPU port. Attempted only once C, D, and F have made agent state a flat SoA with a CSR grid and no host coupling. |

### Performance log

Each row is a measured change, not a plan.

| Date | Change | Agents | Tick p50 | State digest | Notes |
| --- | --- | --- | --- | --- | --- |
| 2026-08-19 | Phase A baseline, Release, 15 threads, AMD Ryzen 7 7700X | 10,000 | 3.3468 ms | `c68dedbbad082126` | Includes work-counter instrumentation overhead (see above); no profiler has run yet. |

## The viewer

The interactive `tactix` binary is how the simulation gets looked at, and it is held to a different
standard than the rest of this page: nothing here is a performance claim, it is a legibility one.
The question the view has to answer is what ten thousand agents are *doing*, at whatever scale you
are watching them from.

![The same battle at 2.0x zoom: infantry arrowheads pointing where they move, archers as squares,
arrows arcing between the lines, a broken squad drained of its team colour, and blood on the ground
under the fighting.](docs/images/melee.png)

The rule the whole view follows is that each visual channel carries exactly one fact. Overloading
one channel twice is what made the earlier view unreadable at a distance, and it is why nothing in
the UI chrome is blue or red any more.

| Channel | Carries |
| --- | --- |
| Hue | Team, and only team |
| Shape | Unit type. Infantry are a short broad arrowhead, cavalry a long narrow dart, archers a square. The three differ in proportion rather than size, because at four pixels apart size alone is not a difference. |
| Brightness | Health, so a worn-down line is visible before it breaks rather than only when it vanishes |
| Desaturation | The squad has broken. It keeps its silhouette and loses the colours it is no longer fighting for. |

Depth comes from a single light direction shared by everything that casts a shadow. Buildings are
drawn as the collision footprint with a roof offset toward the light, so the overhang is what reads
as height. Arrows ride the trajectory they are already flying rather than a curve invented for the
view: `liveAfter` is `kArrowArcFraction` of the shot's length, so the same number that decides when
an arrow can hit decides how high it sits above its own ground shadow.

The ground is procedural in two layers, and the second one is the point. A single texture stretched
over the world is fine at the fitted zoom and turns to soup by 2x, so a seamless grain layer is
tiled at a fixed *world* scale on top of it. It gains detail as you zoom in instead of losing it,
and fades out below 0.5x where one texel is under a screen pixel and the layer is nothing but
aliasing.

Everything expensive is gated on how many screen pixels a world pixel is worth rather than on a
quality setting, so outlines, body shadows, health pips and grain appear when they would actually
resolve. At the far end a squad overlay takes over, drawing each squad as a disc sized by head
count with a morale ring around it. The disc scales by area rather than radius, because a squad
twice the size should look twice the force. It fades out again once individual men are legible.

Deaths leave a mark, which needs the one simulation-side hook in the renderer's favour: a
presentation-only death log, filled in the window between `recordCasualties` (which marks a soldier
dead) and `compactDead` (which removes the body). That is the only point in the tick where a corpse
still has a position. It is opt-in and set only by `main`, because `tactix_bench` shares the
`Simulation` class and must not be charged for a feature that only the window uses. The state
digest does not see it.

### Controls

| Input | Does |
| --- | --- |
| Mouse wheel | Zoom, holding the point under the cursor still for the whole eased transition |
| Right or left drag | Pan |
| `W` `A` `S` `D` or arrows | Pan, at a constant apparent rate regardless of zoom |
| `F` or middle click | Fit the whole world |
| `Space` | Pause and resume |
| `[` `]` | Halve or double the time scale; `Backspace` resets it to 1x |
| `G` | Grid |
| `Tab` | Squad overlay |
| `F12` | Screenshot to `tactix.png` |

**No frame-rate number appears in this section, for the same reason none appears above.** The draw
cost has been observed while building this, but no committed command reproduces it, so it is not
published. `tactix_bench` remains headless and never opens a window, and every measured figure on
this page comes from it.

## Layout

| Path | What |
| --- | --- |
| [`c++/`](c++) | The simulation engine. Start with its [README](c++/README.md). |
| [`tactix-ai/`](tactix-ai) | Rust experiments on agent behavior and scenario loading |

## Building

Dependencies are pulled by CMake FetchContent at configure time, so the first configure needs
network access. There are no submodules to initialise (a stale `.gitmodules` referencing Tracy
and spdlog existed from a pre-C++20/raylib pivot and has been removed).

```
cd c++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or use the wrapper scripts, which do the same and can run the result:

```
c++/scripts/build.sh -r      # macOS/Linux: build Release, then run the GUI
c++\scripts\build.bat -r     # Windows: same
```

`-t` runs the tests instead, `-b` the benchmark (any remaining arguments are forwarded to it),
`-d` builds Debug into `build-debug/`. `-h` prints the flags.

## Built with

C++20, CMake, [raylib](https://github.com/raysan5/raylib), Dear ImGui (docking) via rlImGui,
spdlog.
