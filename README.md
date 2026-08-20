# Tactix

Data-oriented agent simulation in C++. Ten thousand autonomous agents at 60 ticks per second.

The point of the project is the performance work rather than the scenario. Every feature exists
to put pressure on memory layout, spatial queries, or parallelism. Phase A of the project's
[performance-foundation design spec](docs/superpowers/specs/2026-08-19-tactix-performance-foundation-design.md)
replaced an unverifiable `~1.6 ms` claim with a deterministic simulation, a headless benchmark
harness, and a CI gate — the numbers below are what that harness actually measured, and the
"Reproducing these numbers" section is the command that produced them.

## Current numbers

Measured with `tactix_bench --agents 10000 --ticks 2000 --seed 42 --json` (Release build), on:

- AMD Ryzen 7 7700X (8 physical cores / 16 logical), Windows 11 Pro 10.0.22621, MSVC 19.29.30153.0,
  Release build
- 15 worker threads (`hardware_concurrency() - 1`)

| Metric | Value |
| --- | --- |
| Agents | 10,000 |
| Simulation rate | 60 ticks/sec, fixed timestep |
| Tick cost, p50 | 3.3468 ms |
| Tick cost, p95 | 3.4773 ms |
| Tick cost, p99 | 3.5627 ms |
| Tick cost, max | 4.6678 ms |
| Worker threads | 15 (this machine); thread count does not change simulation state (see below) |
| Agent state | structure of arrays (see `EntityHot` in [`c++/src/Simulation.hpp`](c++/src/Simulation.hpp)) |
| Neighbor query | uniform grid hash, 3x3 cell lookup |
| State digest (seed 42) | `c68dedbbad082126` |

**This tick cost includes work-counter instrumentation overhead.** `tactix_bench` increments four
`std::atomic` counters (`WorkCounters`, see [`c++/src/WorkCounters.hpp`](c++/src/WorkCounters.hpp))
roughly 216,000 times per tick from worker threads, and the counters share a cache line. That
contention is real and is baked into every number above; it was deliberately left in place because
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

## Built with

C++20, CMake, [raylib](https://github.com/raysan5/raylib), Dear ImGui (docking) via rlImGui,
spdlog.
