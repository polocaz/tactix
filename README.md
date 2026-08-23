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
| Tick cost, p50 | 3.5824 ms |
| Tick cost, p95 | 6.1573 ms |
| Tick cost, p99 | 7.5088 ms |
| Tick cost, max | 12.5709 ms |
| Worker threads | 15 (this machine); thread count does not change simulation state (see below) |
| Agent state | structure of arrays (see `SoldierHot` in [`c++/src/Simulation.hpp`](c++/src/Simulation.hpp)) |
| Neighbor query | uniform grid hash, 3x3 cell lookup |
| State digest (seed 42) | `5b01c0c4e1e73d6b` |

These are **down** from a previously published p50 of 8.6383 ms, and unlike the drop before them,
this one is speed rather than pacing. Same seed, same tick count, same battle: `stateDigest` reads
`5b01c0c4e1e73d6b` before and after, so not one soldier ends up standing anywhere different. The
whole 2.4x came out of the neighbour path, in three changes, each measured by reverting only itself
from the shipped build.

| Configuration | Tick p50 | Costs |
| --- | --- | --- |
| As shipped | 3.7090 ms | |
| Work counters incremented per cell again, everything else kept | 7.6929 ms | **3.98 ms** |
| Separation and melee walk the grid separately again, everything else kept | 4.7816 ms | **1.07 ms** |
| The committed code before this change | 8.6383 ms | |

Those four rows are one back-to-back session on one machine and belong to each other, not to the
table above it: the shipped row reads 3.7090 ms where the headline says 3.5824 ms, because the
headline triple was taken later on a quieter machine. Compare the rows to each other, and take the
gaps rather than the absolute values as the result.

**The instrumentation was the bottleneck, not the simulation.** `SpatialHash::queryNeighbors` used
to increment two shared `std::atomic` counters once per cell visited, which at this configuration is
about 554,000 contended read-modify-writes per tick across 15 worker threads, on two counters that
share a cache line. They now accumulate in locals and flush once per query, about 43,000 atomics
instead. The totals are identical, which is the point: `candidatesExamined` and `cellsVisited` are
still exactly the same numbers, counted in a way that does not serialise the threads producing them.
That answers the question the previous version of this section left open for Phase B's profiler. The
answer is 3.98 ms, which is more than the entire tick now costs.

**The second walk was doing no work.** The steering phase asked the grid for the 3x3 block around a
soldier twice per tick, once for separation and again inside `selectMeleeTarget`
([`c++/src/Combat.cpp`](c++/src/Combat.cpp)), from the same position on the same tick.
`queryNeighbors` ignores its radius argument and always returns that fixed block, so the second walk
returned the first walk's list every time. They now share one walk, which is where `cellsVisited`
falls from 553,993,549 to 387,282,774 and `candidatesExamined` from 37.77 to 26.43 billion. Every
other counter, and the digest, is unchanged across it: see the note in
[`c++/tests/baseline/counters-2k-1600.txt`](c++/tests/baseline/counters-2k-1600.txt).

**The three do not add up, and the third one is why.** Candidates are now visited in place instead
of being copied into a scratch vector and read straight back out, about nineteen million ids a tick
that no longer make that round trip. On its own that is a *loss*: with the per-cell counters and the
duplicate walk both restored, visiting in place measured 10.4475 ms against the committed code's
8.6383 ms. The mechanism is that a contended atomic increment interleaved with the consumer's own
per-candidate arithmetic serialises harder than the same increment sitting in a tight loop of its
own, which is what it was when the copy separated the two. Batching the counters is what turns that
change from a cost into a saving, so the ordering is load-bearing rather than incidental.

The `max` figure sits inside the 16.67 ms a 60 Hz frame allows with about 4.1 ms of headroom at
10,000 agents. It is still the tail that is tight, and the tail is also the noisiest thing here:
across every run of this command taken while writing this section, p50 stayed inside 3.57 to 4.08 ms
while `max` ranged from 11.7493 ms all the way to 15.7554 ms. Distrust the tail on a machine doing
anything else. The section below identifies where most of it comes from, which turns out not to be
the phases that dominate the median.

## Where the tick goes

The attribution table above was produced by editing the source, rebuilding, and diffing p50. That
measures the right thing but is not a command anyone else can run, so the breakdown is now built in:

```bash
./build/Release/tactix_bench --agents 10000 --ticks 2000 --seed 42 --profile
```

`--profile` is opt-in, so the headline figures above are never quietly a profiled run's figures.
Taking a profile does not change the simulation: `stateDigest` and every work counter are identical
with it on and off, which
[`c++/tests/test_determinism.cpp`](c++/tests/test_determinism.cpp) asserts rather than leaving to
inspection. Its own overhead is eleven `steady_clock` reads per tick and does not resolve above
run-to-run noise here; three profiled runs gave p50s of 3.79/3.81/3.91 ms against 4.08/4.01/3.95 ms
unprofiled, which supports "too small to see on this machine" and not "zero".

Median of three runs (tick mean 4.1000 ms; the three agreed on every share to within 0.2 points).
The tick *mean* is well above the 3.5824 ms p50, and the last row of commentary below is why: a
serial phase spikes on one tick in fifteen and drags the mean right while leaving the median alone.

| Phase | Mean ms | Share | p50 ms | p95 ms | max ms |
| --- | --- | --- | --- | --- | --- |
| `soldierSteer` | 1.4698 | **35.9%** | 1.3300 | 2.2801 | 3.0032 |
| `contact` | 1.0816 | **26.4%** | 0.9680 | 1.7874 | 2.5051 |
| `squadDecide` | 0.8272 | **20.2%** | 0.7787 | 1.2652 | 1.7699 |
| `squadAggregate` | 0.2355 | 5.7% | 0.2240 | 0.3440 | 0.4795 |
| `armyDecide` | 0.1730 | 4.2% | 0.0014 | 2.5347 | 5.0031 |
| `movement` | 0.1117 | 2.7% | 0.1105 | 0.1386 | 0.2789 |
| `spatialHash` | 0.0881 | 2.2% | 0.0868 | 0.0996 | 0.1890 |
| `resolution` | 0.0799 | 1.9% | 0.0780 | 0.0911 | 0.2145 |
| `projectiles` | 0.0162 | 0.4% | 0.0002 | 0.0432 | 0.1912 |
| `clampToWorld` | 0.0113 | 0.3% | 0.0110 | 0.0133 | 0.0944 |
| `snapshotPrev` | 0.0057 | 0.1% | 0.0044 | 0.0192 | 0.0380 |
| *(unattributed)* | 0.0001 | 0.0% | | | |

**Share is computed from means, and that is not a stylistic choice.** Percentiles do not add: the
tick's p50 is not the sum of the phases' p50s, because the phase that spikes on one tick is not the
one that spikes on the next, and a phase's own median tick is generally not the tick whose total
lands on the median. Means add exactly, so a column built from them sums to the tick and a column
built from percentiles would sum to nothing in particular. The `unattributed` row is the gap between
the tick as timed from outside and the phases as timed from inside, and it is published because that
row is also where a phase nobody remembered to mark would show up.

**The top three costs are 82.4 percent of the tick, and two of them are the same thing.**
`soldierSteer` and `contact` are both neighbour walks: one for separation and melee, one for
non-penetration. Between them they are 62.3 percent of the tick, which is what makes the grid the
right thing to keep working on and Phase C the right next move.

**`squadDecide` at 20.2 percent was not predicted by anything.** It does not touch the grid. It is
800,000 squad decisions over the run, about 2.1 microseconds each, and no phase of the roadmap
mentions it. It is now the largest cost in the tick that nobody has looked at.

**The tail is a serial phase, not a parallel one.** `armyDecide` has a p50 of 0.0014 ms and a p95 of
2.5347 ms, because it runs on 2 ticks in 30 (`kArmyDecideInterval`, see
[`c++/src/Units.hpp`](c++/src/Units.hpp)) and is single-threaded when it does: 0.0667 x 2.6 ms +
0.933 x 0.0014 ms reproduces its 0.173 ms mean, so essentially all of it lands on one tick in
fifteen. That is 2.6 ms of one-threaded work dropped into a 4 ms tick, and it is the single largest
identified contributor to the p95 and max figures the section above calls tight. Fifteen worker
threads are idle for the whole of it.

The 9.0937 ms published before that had one named cause of its own: soldiers collide with each other.
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

**This tick cost still includes work-counter instrumentation overhead, but far less of it.**
`tactix_bench` increments seven `std::atomic` counters (`WorkCounters`, see
[`c++/src/WorkCounters.hpp`](c++/src/WorkCounters.hpp)) from worker threads, and the counters still
share a cache line. What changed is the rate: the two counters that dominated the traffic are now
flushed once per query rather than once per cell visited, so the contended traffic drops by about
13x, and recovering that contention is the 3.98 ms the table above attributes to it. The
per-worker cache-line-padded counter block is still the proper fix and is still unbuilt; it is worth
less now than it was, and it is still the next thing to try if this line item matters again.

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
`stateDigest = 5b01c0c4e1e73d6b`, with `p50Ms` of 3.5696, 3.5815, and 3.5824 (the table above uses
the third run). The same digest also comes back at `--threads 1` and `--threads 4`, which is the
thread-invariance property stated above exercised by hand rather than only by the test suite.

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
| B | Integrate a profiler; publish the actual tick breakdown; name the top three costs with evidence. **Complete.** `tactix_bench --profile` reports a per-phase breakdown that partitions the tick; see "Where the tick goes" above. The top three are `soldierSteer` (35.9%), `contact` (26.4%) and `squadDecide` (20.2%). It is not a sampling profiler and does not claim to be: it is eleven timestamps around the phases `Simulation::tick` already had, which is enough to rank them and not enough to look inside one. |
| C | Flat CSR spatial grid (counts → prefix sum → dense payload); query returns an index range instead of copying. The copying half is done: queries visit candidates in place through `SpatialHash::forEachNeighbor`. The flat CSR storage is not. |
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
| 2026-08-23 | One neighbour walk per soldier, candidates visited in place, work counters flushed per query instead of per cell | 10,000 | 3.5824 ms | `5b01c0c4e1e73d6b` | Down from 8.6383 ms. Digest held FIXED, so this is speed and not a behavioural change. Attribution table above; counter contention was 3.98 ms of it. |

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
