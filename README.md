# Tactix

Data-oriented agent simulation in C++. Ten thousand autonomous agents at 60 ticks per second,
with a full tick costing roughly 1.6 ms of a 15 ms budget.

The point of the project is the performance work rather than the scenario: structure-of-arrays
memory layout at about 32 bytes per agent, a fixed-timestep accumulator with interpolated
rendering, seven worker threads, and Tracy instrumentation so the cost of each phase is measured
instead of guessed at.

The current scenario is a zombie outbreak. Three agent types with seek and flee behaviors, group
coordination, ranged combat, environment obstacles, and hard collision physics.

## Layout

| Path | What |
| --- | --- |
| [`c++/`](c++) | The simulation engine, and the detailed writeup in [`c++/README.md`](c++/README.md) |
| [`tactix-ai/`](tactix-ai) | Rust experiments on agent behavior |

## Built with

C++17, CMake, Dear ImGui, [Tracy](https://github.com/wolfpld/tracy), spdlog.
