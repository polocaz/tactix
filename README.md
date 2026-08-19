# Tactix

Data-oriented agent simulation in C++. Ten thousand autonomous agents at 60 ticks per second,
with a full tick costing roughly 1.6 ms of a 15 ms budget.

The point of the project is the performance work rather than the scenario. Every feature exists
to put pressure on memory layout, spatial queries, or parallelism, and every claim below is
measured rather than estimated.

## Current numbers

| Metric | Value |
| --- | --- |
| Agents | 10,000 |
| Simulation rate | 60 ticks/sec, fixed timestep |
| Tick cost | ~1.6 ms, 10.7% of the 15 ms budget |
| Render rate | 144 FPS, interpolated from 60 TPS |
| Worker threads | 7 |
| Agent state | ~32 bytes, structure of arrays |
| Neighbor query | uniform grid hash, 3x3 cell lookup, ~100 to 200 candidates instead of 10,000 |

Full feature breakdown and phase history in [`c++/README.md`](c++/README.md). Architecture and
performance budgets in [`c++/docs/Design Document.md`](c++/docs/Design%20Document.md).

## Where this is going

**Phase 5 is a GPU port, targeting 500,000 agents.**

The simulation is currently CPU-bound and parallel across seven threads. Agent updates and
neighbor queries are both embarrassingly parallel and memory-bound, which is the shape of
problem GPUs exist for. The work is to move agent state resident to the device, rebuild the
spatial grid as a GPU structure, and keep the host out of the per-frame path entirely.

The measurement discipline stays the same. Nothing goes in this README that was not profiled.

### Roadmap

| Phase | Goal | Status |
| --- | --- | --- |
| 1 to 4.5 | CPU simulation, spatial grid, job system, combat and environment | Complete |
| 5.1 | Profile the current tick and name the top three costs | Next |
| 5.2 | Agent state resident on GPU, no per-frame host transfer | |
| 5.3 | GPU spatial grid, rebuilt or refit per frame | |
| 5.4 | Steering and collision as kernels | |
| 5.5 | 500,000 agents, published before and after | |

### Performance log

Each row is a measured change, not a plan. Empty until 5.1 lands.

| Date | Change | Agents | Tick | Notes |
| --- | --- | --- | --- | --- |
| | baseline, CPU, 7 threads | 10,000 | 1.6 ms | |

## Layout

| Path | What |
| --- | --- |
| [`c++/`](c++) | The simulation engine. Start with its [README](c++/README.md). |
| [`tactix-ai/`](tactix-ai) | Rust experiments on agent behavior and scenario loading |

## Building

```
cd c++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Submodules are required:

```
git submodule update --init --recursive
```

## Built with

C++17, CMake, SDL2, Dear ImGui, [Tracy](https://github.com/wolfpld/tracy), spdlog.
