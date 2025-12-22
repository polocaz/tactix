# Tactix

**A high-performance, data-oriented simulation engine demonstrating 10,000+ autonomous agents with deterministic behavior.**

Tactix is designed to showcase systems programming expertise through cache-friendly memory layouts, fixed timestep simulation, and measurable performance optimization.

---

## 🎯 Current Status: Phase 3 Complete ✅

**Performance Target:** 10,000 agents @ 60 ticks/sec with parallelization  
**Achieved:** ~1.6ms tick time (10.7% of 15ms budget) with 7 worker threads

### Implemented Features

#### Phase 1: Foundation
- ✅ **Fixed Timestep Accumulator** - Deterministic 60 TPS simulation with interpolated rendering
- ✅ **Structure of Arrays (SoA) Layout** - Cache-friendly memory organization (~24 bytes/agent)
- ✅ **Performance Metrics Dashboard** - Real-time tick time monitoring with 60-frame rolling average
- ✅ **Interpolated Rendering** - Smooth 144 FPS visuals from 60 TPS simulation

#### Phase 2: Spatial Partitioning
- ✅ **Uniform Grid Hash** - O(1) spatial queries with 50-pixel cells
- ✅ **Neighbor Queries** - 9-cell (3x3) lookups checking ~100-200 entities vs all 5k
- ✅ **Collision Avoidance** - Separation steering with distance-based forces
- ✅ **Debug Visualization** - Toggleable grid overlay showing spatial partitioning
- ✅ **5,000 Agent Simulation** - Emergent flocking behavior with local interactions

#### Phase 3: Job System & Parallelization
- ✅ **Worker Thread Pool** - (hardware_concurrency - 1) threads with job queue
- ✅ **Parallel Entity Updates** - 256-agent chunks distributed across workers
- ✅ **Barrier Synchronization** - waitAll() for phase completion
- ✅ **Thread Metrics** - Jobs/frame, worker count, speedup tracking
- ✅ **10,000 Agent Simulation** - 3.5x speedup from parallelization
- ✅ **Rendering Optimization** - Directional triangles, 144 FPS @ 10k agents
- ✅ **macOS Retina Fix** - FLAG_WINDOW_HIGHDPI for correct viewport scaling

### Technical Highlights

**Architecture:** Decoupled simulation (60 TPS) and presentation (variable FPS) layers  
**Memory Layout:** Hot data (position, velocity, state) stored in separate contiguous arrays  
**Spatial Partitioning:** Uniform grid hash enables O(n) neighbor queries instead of O(n²)  
**Determinism:** Fixed timestep ensures identical results across different hardware  
**Rendering:** Alpha blending between previous/current state for sub-frame interpolation

---

## 🚀 Build Instructions

### Prerequisites

- CMake 3.20+
- C++20 compiler (Clang/GCC/MSVC)
- Command Line Tools (macOS) or build-essential (Linux)

### Build & Run

```bash
# Clone repository
git clone <repository-url>
cd tactix

# Configure and build
mkdir build && cd build
cmake ..
cmake --build .

# Run simulation
./tactix
```

### Build Options

```bash
# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug build (with symbols)
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

---

## 📊 Performance Metrics

| Metric | Phase 1 Target | Phase 2 Target | Phase 3 Target | Actual |
|--------|----------------|----------------|----------------|--------|
| Agent Count | 1,000 | 5,000 | 10,000 | 10,000 |
| Tick Rate | 60 TPS | 60 TPS | 60 TPS | 60 TPS (fixed) |
| Tick Time | < 1.5 ms | < 7.5 ms | < 15 ms | ~1.6 ms ✅ |
| Worker Threads | N/A | N/A | 4-8 | 7 (M1/M2) |
| Jobs/Frame | N/A | N/A | ~80 | 80 (40×2 phases) |
| Speedup | N/A | N/A | 3-4x | ~3.5x ✅ |
| Spatial Hash | N/A | < 2 ms | < 2 ms | ~0.5-1 ms ✅ |
| Memory per Agent | 24 bytes | 24 bytes | 24 bytes | 32 bytes (dirX/Y added) |
| Render FPS | 100-144+ | 100-144+ | 60+ | 144 FPS ✅ |

*Tested on: Apple M1/M2 (arm64)*

**Performance Win:** Spatial partitioning reduces collision checks from O(n²) = 25M to O(n) = ~500k (**50x faster**) ⚡

---

## 📁 Project Structure

```
tactix/
├── src/
│   ├── main.cpp           # Entry point, fixed timestep loop
│   ├── Simulation.hpp     # Core simulation orchestration
│   ├── Simulation.cpp     # SoA entity management & systems
│   ├── SpatialHash.hpp    # Uniform grid hash for neighbor queries
│   ├── SpatialHash.cpp    # Spatial partitioning implementation
│   ├── JobSystem.hpp      # Worker thread pool for parallelization
│   ├── JobSystem.cpp      # Job queue & barrier synchronization
│   └── Agent.hpp          # (Legacy, unused)
├── docs/
│   ├── Design Document.md # Detailed architecture & algorithms
│   └── Roadmap.md        # 7-week implementation plan
├── CMakeLists.txt        # Build configuration
└── README.md             # This file
```

---

## 🗺️ Roadmap

### ✅ Phase 1: Single-Threaded Baseline (Complete)
- Fixed timestep accumulator
- SoA memory layout
- Basic movement system
- Performance metrics

### ✅ Phase 2: Spatial Partitioning (Complete)
- Uniform grid hash for neighbor queries
- Collision avoidance behavior
- 5,000 agents @ 60 TPS
- Debug visualization

### ✅ Phase 3: Job System & Parallelization (Complete)
- Multi-threaded worker pool (7 threads)
- Parallel entity updates (256-agent chunks)
- 10,000 agents @ 60 TPS
- 3.5x speedup achieved

### 📅 Phase 4-6: Planned
- Utility-based AI state machine
- Tracy profiler integration & SIMD optimization (20k agents)
- Polish & documentation

See [docs/Roadmap.md](docs/Roadmap.md) for detailed milestones.

---

## 🎮 Controls

- **ESC** - Exit application
- **Show/Hide Grid** - Toggle spatial partitioning visualization
- **ImGui Panel** - View real-time performance metrics

---

## 📖 Documentation

- **[Design Document](docs/Design%20Document.md)** - Architecture, data structures, algorithms, performance budgets
- **[Roadmap](docs/Roadmap.md)** - Phase-by-phase implementation plan with targets

---

## 🛠️ Dependencies (Managed via CMake FetchContent)

- [Raylib](https://github.com/raysan5/raylib) - Graphics & window management
- [Dear ImGui](https://github.com/ocornut/imgui) - Debug UI
- [rlImGui](https://github.com/raylib-extras/rlImGui) - Raylib-ImGui bridge
- [spdlog](https://github.com/gabime/spdlog) - Fast logging

---

## 📝 License

*License information to be added*

---

## 🎯 Design Goals

1. **Performance** - Demonstrate measurable optimization (profiling, flamegraphs, data-oriented design)
2. **Determinism** - Fixed timestep ensures reproducible results
3. **Scalability** - Target 10,000+ agents through architectural design
4. **Portfolio Quality** - Clean code, comprehensive documentation, benchmark results

Built as a technical showcase for systems programming & performance engineering roles.
