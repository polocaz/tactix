# Tactix

**A high-performance, data-oriented simulation engine demonstrating 10,000+ autonomous agents with deterministic behavior.**

Tactix is designed to showcase systems programming expertise through cache-friendly memory layouts, fixed timestep simulation, and measurable performance optimization.

---

## 🎯 Current Status: Phase 1 Complete ✅

**Performance Target:** 1,000 agents @ 60 ticks/sec  
**Achieved:** < 1.5ms tick time (well under 16.66ms budget)

### Implemented Features

- ✅ **Fixed Timestep Accumulator** - Deterministic 60 TPS simulation with interpolated rendering
- ✅ **Structure of Arrays (SoA) Layout** - Cache-friendly memory organization (~24 bytes/agent)
- ✅ **Performance Metrics Dashboard** - Real-time tick time monitoring with 60-frame rolling average
- ✅ **Interpolated Rendering** - Smooth 144 FPS visuals from 60 TPS simulation
- ✅ **1,000 Agent Simulation** - Simple wander behavior with screen wrapping

### Technical Highlights

**Architecture:** Decoupled simulation (60 TPS) and presentation (variable FPS) layers  
**Memory Layout:** Hot data (position, velocity, state) stored in separate contiguous arrays  
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

| Metric | Target | Actual |
|--------|--------|--------|
| Agent Count | 1,000 | 1,000 |
| Tick Rate | 60 TPS | 60 TPS (fixed) |
| Tick Time | < 1.5 ms | ~0.3-0.8 ms ✅ |
| Memory per Agent | < 50 bytes | 24 bytes ✅ |
| Render FPS | Variable | 100-144+ FPS |

*Tested on: Apple M1/M2 (arm64)*

---

## 📁 Project Structure

```
tactix/
├── src/
│   ├── main.cpp           # Entry point, fixed timestep loop
│   ├── Simulation.hpp     # Core simulation orchestration
│   ├── Simulation.cpp     # SoA entity management & systems
│   └── Agent.hpp          # (Legacy, unused in Phase 1)
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

### 🔄 Phase 2: Spatial Partitioning (Next)
- Uniform grid hash for neighbor queries
- Collision avoidance behavior
- 5,000 agents @ 60 TPS

### 📅 Phase 3-6: Planned
- Job system & parallelization (10k agents)
- Utility-based AI state machine
- Tracy profiler integration & SIMD optimization (20k agents)
- Polish & documentation

See [docs/Roadmap.md](docs/Roadmap.md) for detailed milestones.

---

## 🎮 Controls

- **ESC** - Exit application
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
