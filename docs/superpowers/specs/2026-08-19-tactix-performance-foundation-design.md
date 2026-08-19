# Tactix: Performance Foundation

**Date:** 2026-08-19
**Status:** Approved design, pending implementation plan
**Scope:** Phase A (foundation) specified in full; Phases B–H sequenced and justified, specified at intent level.

---

## 1. Goal

Make Tactix a credible systems-engineering portfolio piece by making its performance
claims reproducible, then improving them in measured increments.

The target specialization is **large-scale agent simulation on a single machine** —
maximum throughput via memory layout, spatial data structures, parallelism, and
eventually GPU compute. Distributed simulation is explicitly out of scope.

The artifact that matters to a reviewer is not the final agent count. It is the chain
of reasoning: a profile that identified a cost, a change that addressed it, and a
measurement that confirmed it. The repository should make that chain visible in its
commit history.

---

## 2. Current state

Findings below are from reading the tree at commit `b196a68`, not from assumption.

### 2.1 The README is unverifiable

`README.md` states "every claim below is measured rather than estimated" and
"Nothing goes in this README that was not profiled," and publishes a table including
`~1.6 ms` tick cost at 10,000 agents.

The repository contains no benchmark, no test, no CI, no profiler integration, and no
headless execution path. Every published number requires a GUI window and a human
reading an ImGui overlay. None of it is reproducible by a third party.

This is the single highest-priority problem. Unverifiable claims are worse than modest
ones, because a reviewer who checks finds the gap.

### 2.2 Data race on the RNG (live bug)

`Simulation.cpp` calls raylib's `GetRandomValue` 51 times in the tick path, including
from inside functions dispatched to worker threads:

- `updateSeparationChunk` — lines 388, 389, 409, 410
- `updateBehaviorsChunk` — lines 740, 953, 995, 996

`GetRandomValue` wraps a global `rand()`-style generator with no internal
synchronization. Seven worker threads mutate that shared state concurrently every
tick. This is undefined behavior today. It will rarely manifest as a crash; it
manifests as a silently corrupted random stream and non-reproducible runs — which
would make any benchmark built on top of it meaningless.

### 2.3 Coupling is narrower than it looks

`Simulation.cpp` is 1,671 lines, but:

- All 14 `Draw*` calls are inside `draw()` (line 1512 onward). **The tick path
  performs no rendering.**
- `Simulation.hpp`'s `#include <raylib.h>` exists solely to reach `GetRandomValue`.
  No raylib *type* appears in that header.

Therefore removing the RNG dependency removes the simulation's dependency on raylib
almost entirely. The headless split is a small change, not a rewrite.

### 2.4 Spatial grid structure

`SpatialHash` stores `std::vector<std::vector<uint32_t>> cells`.

- At the configured 50px cell size over a 1280×720 world the grid is 26×15 = **390
  cells**, each an independent heap allocation. (Cell count scales with world size;
  the allocation-per-cell property is what matters, not the current magnitude.)
- `clear()` walks every cell every tick, touching hundreds of scattered heap blocks.
- `queryNeighbors` copies: for each agent it visits up to 9 cells and `insert`s their
  contents into an output vector. Candidates are copied out only to be iterated once
  and discarded. Both separation and behaviors query per agent per tick.

### 2.5 Serial work in `tick()`

`tick()` performs a full linear scan of all agents to find heroes that just fired
(lines ~198 onward), before any parallel phase begins. With ~50 heroes among 10,000
agents this is ~9,950 wasted iterations per tick.

### 2.6 Smaller items

- `spdlog::info` is called from inside the tick on hero death — formatting and I/O in
  the hot path.
- `lastSeenX` / `lastSeenY` are overloaded to carry agent *indices* encoded as floats
  (the code comments call this a "hack"). Numerically safe below 2^24, but it gives
  those arrays two meanings depending on agent state.
- `Agent.hpp` is dead code — nothing includes it; superseded by the SoA layout.
- `Simulation::neighborBuffer` is reserved in the constructor and never used. The chunk
  functions correctly use stack-local buffers. Dead state, not a race.
- `JobSystem::waitAll()` evaluates `jobQueue.empty()` while holding only `waitMutex`,
  not `queueMutex` — a data race on the queue, distinct from §2.2. Pre-existing and
  benign in practice; deferred to Phase E with the rest of the job system. First
  suspect if the thread-invariance test proves flaky.
- `set(CMAKE_CXX_FLAGS "-std=c++20")` is GCC/Clang syntax and breaks MSVC.
- `raylib` is fetched at `GIT_TAG master` — builds are not reproducible across time.

---

## 3. Approach

Three approaches were considered:

- **A — Build the instrument first, then optimize down the list.** Establish headless
  execution, determinism, and benchmarking; then profile; then optimize in measured
  increments.
- **B — Rewrite the core clean, port the scenario onto it.** Cleaner end state, but
  discards the "before" and with it the before/after narrative.
- **C — Scale-first spike.** Point at 500k agents and fix what breaks. Fastest to a
  large number, but is optimization without measurement.

**Approach A was selected.** Rationale:

1. The reasoning chain is the portfolio artifact; A produces it as a byproduct.
2. It makes the README true, closing the credibility gap in the first phase.
3. Decoupling is load-bearing for everything downstream, including the GPU port.

The zombie scenario is **retained, behind a boundary** — the simulation core becomes
scenario-agnostic while the scenario itself remains as the benchmark workload. It is
kept because its clustering behavior stresses the spatial grid harder than a uniform
synthetic workload would, and because a watchable demo has independent value.

---

## 4. Design § 1 — The sim/render boundary

### 4.1 Deterministic RNG

Replace all 51 `GetRandomValue` call sites with a stateless hash-based generator:

```
value = pcg_hash(worldSeed ^ agentIndex * K1 ^ tickNumber * K2 ^ callSite)
```

**No shared mutable state**, therefore no data race and no lock. Because the result
depends only on `(seed, agentIndex, tickNumber, callSite)` and never on scheduling
order, the simulation produces **bit-identical results at any thread count**.

That property is the foundation for the benchmark harness, for replay-from-seed, and
for validating every future concurrent data structure.

Implementation notes:

- Hand-rolled PCG32, approximately 15 lines.
- **Do not use `std::uniform_int_distribution`.** Its output is not specified to be
  identical across standard library implementations, which would break cross-platform
  reproducibility.
- `callSite` is an `enum class RngUse : uint32_t`. Distinct uses within one agent-tick
  (e.g. choosing a patrol target vs. jittering velocity) must not collide. An enum was
  chosen over a threaded per-agent counter because it is greppable and cannot drift.
  Cost: all 51 call sites are touched.

### 4.2 Determinism constraint

Bit-identical reproducibility constrains the floating-point environment. Four separate
mechanisms can break it, and all four must be closed:

**1. Reassociation.** `-ffast-math` / `/fp:fast` permanently off. Later SIMD work
(Phase G) must preserve summation order.

**2. FMA contraction.** `-ffp-contract=fast` is the *default* on GCC and Clang. It
fuses `a*b + c` into a single FMA instruction, which rounds once instead of twice and
therefore produces different results from the same source. This must be explicitly
disabled — it is not covered by turning off fast-math:

- GCC / Clang: `-ffp-contract=off`
- MSVC: `/fp:precise` (and do not enable `/fp:contract`)

**3. x87 excess precision.** 32-bit x86 targets evaluate at 80-bit internally and
round unpredictably on spill. Avoided by requiring a 64-bit target (SSE2 math). If a
32-bit build is ever needed: `-mfpmath=sse -msse2`.

**4. Transcendental functions.** `std::sqrt` is safe — IEEE-754 requires it correctly
rounded, so all 22 uses in `Simulation.cpp` are bit-identical on every conforming
platform. `std::sin` is **not** — glibc, the MSVC CRT, and Apple's libm each produce
different last-bit results, and the tick calls it at lines 633 and 636 (combat shake
and push/pull, both of which feed into agent position and therefore into the digest).

Resolution: replace those two calls with an in-repo polynomial sine approximation
(~10 lines). Two call sites is a small price for a determinism claim that holds across
operating systems, which is what makes the cross-platform CI digest check meaningful.
If additional transcendentals are introduced later they must come from the same
in-repo implementation, never from `<cmath>`.

Accepted deliberately. Determinism is rarer and more demonstrable than the small gain
fast-math would provide, and mechanisms 2 and 4 are exactly the kind of detail whose
absence would make the claim quietly false rather than loudly broken.

### 4.3 Target structure

| Target | Contents | Links raylib |
| --- | --- | --- |
| `tactix_sim` (static lib) | `Simulation`, `SpatialHash`, `JobSystem`, `Rng` | no |
| `tactix` (exe) | `main.cpp`, `Renderer.cpp`, ImGui overlay | yes |
| `tactix_bench` (exe) | headless driver, fixed workload | no |
| `tactix_tests` (exe) | unit + determinism tests | no |

`draw()` moves from `Simulation.cpp` into `Renderer.cpp` and takes a
`const Simulation&`, reading state through a single added const accessor rather than a
proliferation of getters.

### 4.4 Deletions

- `Agent.hpp`
- `Simulation::neighborBuffer`

### 4.5 Explicitly not in this phase

`updateBehaviorsChunk` (~500 lines) is left intact. Splitting it is deferred to Phase F
so that the determinism digest can prove the split is behavior-preserving. Refactoring
before that test exists would be refactoring blind. The same reasoning defers
`JobSystem` changes to Phase E.

---

## 5. Design § 2 — Benchmark harness

### 5.1 Workload

A deterministic replay of the zombie scenario, driven by `(seed, agentCount, tickCount)`:

```
tactix_bench --agents 10000 --ticks 2000 --seed 42 --threads 7 --json
```

The population mutates across a run (agents die, corpses reanimate, combat clusters
form) but mutates *identically every run*, which is what makes A/B comparison valid. A
synthetic steady-state uniform distribution was rejected: it is easier to optimize
against and would flatter the numbers.

### 5.2 Statistics

Report **p50 / p95 / p99 / max**, not a mean. A mean tick time can sit comfortably
inside budget while individual ticks blow it — the user perceives the tail, not the
average. Additionally report a per-phase split across grid rebuild, separation,
behaviors, and movement, so that a change can be attributed to the phase it targeted.

### 5.3 Two classes of measurement

| Class | Examples | Use |
| --- | --- | --- |
| **Deterministic work counters** | neighbor candidates examined, cells touched, heap allocations per tick, jobs dispatched | **Gated in CI.** Machine-independent. |
| **Wall-clock timings** | p50/p99/max tick, per-phase ms | **Recorded, never gated.** Measured locally, logged with hardware named. |

CI must not gate on wall-clock time. Hosted runners are shared and thermally variable;
a timing threshold produces flaky failures, and a flaky performance gate gets muted,
leaving CI theater.

Counting work instead of timing it makes the gate real: a change that causes the grid
to examine 3x the candidates trips deterministically on any hardware. Candidate count
is also the quantity Phase C actually optimizes — time is downstream of it.

### 5.4 Determinism tests

Hash world state (positions, velocities, states) every 100 ticks into a digest. Assert
digests are identical across:

- two runs at the same seed
- 1 thread vs. 7 threads vs. `hardware_concurrency`
- debug vs. release builds
- Linux vs. Windows (compared in CI against a committed reference digest)

The first three follow from §4.1 and §4.2 mechanisms 1–3. The fourth additionally
requires mechanism 4, and is the reason for replacing `std::sin`.

**Fallback:** if cross-platform identity proves impractical for a reason not
anticipated here, scope the committed reference digest per platform and keep the first
three assertions. Do not weaken the digest itself (e.g. by rounding before hashing) —
a tolerance-based comparison would silently accept the class of scheduling bug this
test exists to catch.

The thread-count test is the one that would have caught the §2.2 race immediately, and
is the standing regression test for every future concurrent structure, including the
Phase H GPU port.

### 5.5 Tooling

- **Benchmark harness: hand-rolled** (~80 lines: `steady_clock`, sorted vector for
  percentiles, JSON writer). Google Benchmark's repeat-until-stable model is designed
  for microbenchmarks and fights a stateful multi-tick simulation.
- **Test framework: doctest** — single header, one `FetchContent` block, gives
  individually named results in CI logs.

### 5.6 CI

One workflow: build on Linux and Windows (fixing the MSVC flag issue in §2.6), run
tests, run the determinism check, run the work-counter gate. Pin the raylib tag.

### 5.7 Work-counter baseline

Expected counts are committed as JSON and diffed by CI. Exact values, not bounds: any
intentional optimization updates the baseline in the same commit, so the diff shows
e.g. `candidatesExamined: 187 -> 61` adjacent to the change that caused it. Those
baseline diffs are the performance story told in the commit log at no extra cost.

---

## 6. Design § 3 — Phase sequencing

### Phase A — Foundation

Everything in §4 and §5, plus the hygiene items in §2.6.

**Exit criterion:** a committed, reproducible baseline obtainable with one command.

### Phase B — Measure

Integrate a profiler. Publish the actual tick breakdown. Name the top three costs with
evidence.

### Phases C–G — Optimization, ordered by prior

**Phase B outranks the ordering below.** The order reflects expectation, not
conclusion; if the profile contradicts it, the profile wins. That is the reason for
building the instrument first.

| | Change | Expected rationale |
| --- | --- | --- |
| **C** | Flat CSR grid: counts → prefix sum → dense payload. Query returns an index range; no copying. | Eliminates thousands of allocations and the per-agent copy (§2.4); makes cell iteration contiguous. Also the structure Phase H requires, so it is paid once. |
| **D** | Reorder agent arrays by cell (or Morton order) each rebuild | Makes neighbors adjacent in memory so the separation inner loop stops striding randomly across 24 arrays. |
| **E** | Replace `std::function` + single-mutex queue with atomic-index `parallel_for` | Removes a heap allocation per job and a global lock. Becomes visible once C and D shorten the tick. |
| **F** | Hot/cold split of `EntityHot`'s 24 arrays; decompose `updateBehaviorsChunk`; remove the §2.5 serial scan and §2.6 hot-path logging | Movement and separation touch ~6 arrays but pay cache cost for combat-only state. Safe here because the determinism digest proves the decomposition changed nothing. |
| **G** | SIMD separation kernel, preserving summation order | Final CPU change, constrained by §4.2. |

### Phase H — GPU

Attempted only after the CPU implementation is measured and ordered. By this point
agent state is a flat SoA with a CSR grid and no host coupling, so the port is
mechanical rather than a rewrite. C, D, and F each independently earn their place on
CPU *and* each remove a specific blocker to H.

---

## 7. Repository hygiene (Phase A)

- **README:** reframe around measurement discipline rather than an imminent GPU port.
  The current roadmap promises 500,000 agents in a document whose performance log is
  empty. After Phase A that table holds a real reproducible row, and the roadmap can
  present C–H as ranked but unproven.
- **`tactix-ai/`:** archive to a branch. Twenty files of Rust halted mid-scenario-loader
  reads as an abandoned pivot. On a branch, history is preserved at no cost.

---

## 8. Non-goals

- Distributed / multi-machine simulation.
- Gameplay features, content, or art.
- Rewriting the simulation core (Approach B was considered and rejected).
- Networking, persistence, or scenario authoring tools.

---

## 9. Success criteria

Phase A is complete when:

1. `tactix_bench` runs headless with no window and no raylib linkage.
2. The same seed produces an identical state digest at 1, 7, and `hardware_concurrency`
   threads, in both debug and release, on both Linux and Windows (per §5.4, with the
   documented per-platform fallback if the cross-platform assertion is dropped).
3. CI builds on Linux and Windows and gates on the work-counter baseline.
4. `README.md` contains a first performance-log row that a third party can reproduce
   with one documented command.
5. No `GetRandomValue` call remains in `tactix_sim`.
6. No `<cmath>` transcendental remains in the tick path (§4.2 mechanism 4); `sqrt` is
   permitted.

The project overall is on track when each subsequent phase adds one row to the
performance log, attributable to one commit, justified by one profile.
