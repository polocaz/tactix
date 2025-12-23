# Tactix - Zombie Survival Simulation

**A high-performance, data-oriented agent simulation engine demonstrating emergent behavior with 10,000+ autonomous agents.**

Tactix showcases systems programming expertise through cache-friendly memory layouts, multi-threaded parallelization, and real-time interactive controls. Now featuring a zombie outbreak simulation with three distinct agent types exhibiting seek/flee behaviors.

---

## 🎯 Current Status: Phase 3 Complete + Zombie Behaviors ✅

**Performance Target:** 10,000 agents @ 60 ticks/sec with parallelization  
**Achieved:** ~1.6ms tick time (10.7% of 15ms budget) with 7-15 worker threads

### Implemented Features

#### Phase 1: Foundation
- ✅ **Fixed Timestep Accumulator** - Deterministic 60 TPS simulation with interpolated rendering
- ✅ **Structure of Arrays (SoA) Layout** - Cache-friendly memory organization (~32 bytes/agent)
- ✅ **Performance Metrics Dashboard** - Real-time tick time monitoring with 60-frame rolling average
- ✅ **Interpolated Rendering** - Smooth 144 FPS visuals from 60 TPS simulation

#### Phase 2: Spatial Partitioning
- ✅ **Uniform Grid Hash** - O(1) spatial queries with 50-pixel cells
- ✅ **Neighbor Queries** - 9-cell (3x3) lookups checking ~100-200 entities vs all 10k
- ✅ **Collision Avoidance** - Separation steering with distance-based forces
- ✅ **Debug Visualization** - Toggleable grid overlay showing spatial partitioning
- ✅ **Emergent Flocking** - Local interactions create cohesive group behaviors

#### Phase 3: Job System & Parallelization
- ✅ **Worker Thread Pool** - (hardware_concurrency - 1) threads with job queue
- ✅ **Parallel Entity Updates** - 256-agent chunks distributed across workers
- ✅ **Barrier Synchronization** - waitAll() for phase completion
- ✅ **Thread Metrics** - Jobs/frame, worker count, speedup tracking
- ✅ **10,000 Agent Simulation** - 3.5x speedup from parallelization
- ✅ **Rendering Optimization** - Directional triangles, 144 FPS @ 10k agents

#### Phase 4: Agent Behaviors & Zombie Simulation
- ✅ **Three Agent Types** - Civilians, Zombies, Heroes with distinct AI
- ✅ **Seek/Flee Steering Behaviors** - Context-aware movement based on nearby agents
- ✅ **Infection Mechanics** - Zombie-civilian collision spreads outbreak
- ✅ **Hero Combat System** - Heroes kill zombies, track health, eventual conversion
- ✅ **Dynamic Population** - Real-time agent type transitions and removals
- ✅ **Visual Feedback** - Color-coded agents, health indicators, population breakdown

#### Quality of Life Features
- ✅ **Camera Controls** - Zoom (0.125x-8x), pan, and reset for detailed observation
- ✅ **Time Scale Control** - Adjust simulation speed (0.125x-4x) with keyboard/slider
- ✅ **Dynamic Agent Count** - Live adjustment from 100-10,000 agents
- ✅ **Wrap Detection Fix** - Prevents interpolation artifacts at world boundaries
- ✅ **Cross-Platform** - Windows/macOS/Linux support with platform-specific fixes

### Technical Highlights

**Architecture:** Decoupled simulation (60 TPS) and presentation (variable FPS) layers  
**Memory Layout:** Hot data (position, velocity, type, health) in separate contiguous arrays  
**Spatial Partitioning:** Uniform grid hash enables O(n) neighbor queries instead of O(n²)  
**Determinism:** Fixed timestep ensures reproducible results (with time scale control)  
**Rendering:** Alpha blending with wrap detection for smooth sub-frame interpolation  
**AI System:** Parallel behavior updates with type-specific seek/flee logic

## 🧟 Zombie Survival Simulation

The simulation models a zombie outbreak with three interacting agent types:

### Agent Types

**Civilians (White/Light Gray)** - 90% of initial population
- Flee from zombies within 150px detection range
- Speed: 90 px/s
- Become zombies on contact with infected

**Zombies (Green)** - 5% of initial population  
- Seek both civilians and heroes aggressively
- Speed: 80 px/s (slower than prey)
- Infect civilians, engage heroes in combat

**Heroes (Blue Gradient)** - 5% of initial population
- Hunt zombies with high aggression
- Speed: 120 px/s (fastest agents)
- Kill 5 zombies before converting to zombie
- Color brightness indicates remaining health

### Mechanics

- **Infection System** - Close contact (15px) converts civilians to zombies
- **Hero Combat** - Heroes kill zombies on contact, lose 1 health per kill
- **Hero Conversion** - After 5 kills (health = 0), hero becomes zombie
- **Population Dynamics** - Live tracking of type distribution and conversions
- **Emergent Behavior** - Complex patterns from simple local interactions

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
│   ├── main.cpp           # Entry point, fixed timestep loop, camera controls
│   ├── platform.h         # Cross-platform Windows API conflict resolution
│   ├── Simulation.hpp     # Core simulation orchestration & agent behaviors
│   ├── Simulation.cpp     # SoA entity management, seek/flee, infection system
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

### ✅ Phase 4: Agent Behaviors (In Progress)
- ✅ Three agent types (Civilian, Zombie, Hero)
- ✅ Seek/flee steering behaviors
- ✅ Infection & combat mechanics
- ✅ Dynamic population tracking
- 🔄 Advanced AI states (planned)
- 🔄 Resource gathering (planned)

### 📅 Phase 5-6: Planned
- Tracy profiler integration & SIMD optimization (20k agents)
- Polish & documentation

See [docs/Roadmap.md](docs/Roadmap.md) for detailed milestones.

---

## 🎮 Controls

### Camera
- **Mouse Wheel** - Zoom in/out (0.125x to 8x)
- **Right Click + Drag** - Pan around simulation world
- **Middle Click** - Reset camera to default view

### Simulation
- **`[`** - Slow down time (0.125x, 0.25x, 0.5x, 1.0x)
- **`]`** - Speed up time (1.0x, 2.0x, 4.0x)
- **Backspace** - Reset time scale to 1.0x
- **Show/Hide Grid Button** - Toggle spatial partitioning visualization

### UI Sliders
- **Total Agents** - Adjust agent count (100-10,000) in real-time
- **Time Scale** - Smooth control over simulation speed

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
