# Tactix - Zombie Survival Simulation

**A high-performance, data-oriented agent simulation engine demonstrating emergent behavior with 10,000+ autonomous agents.**

Tactix showcases systems programming expertise through cache-friendly memory layouts, multi-threaded parallelization, and real-time interactive controls. Now featuring a zombie outbreak simulation with three distinct agent types exhibiting seek/flee behaviors.

---

## 🎯 Current Status: Phase 4.5 Complete - Tactical Survival Simulation ✅

**Performance Target:** 10,000 agents @ 60 ticks/sec with full AI and environment  
**Achieved:** ~1.6ms tick time (10.7% of 15ms budget) @ 144 FPS with 7 worker threads  
**Latest:** Group behaviors, ranged combat, environment obstacles, hard collision physics

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

#### Phase 4: AI State Machine & Memory System
- ✅ **Five-State AI** - Idle, Patrol, Fleeing, Pursuing, Searching with memory persistence
- ✅ **Memory System** - Last-seen positions, search timers, patrol targets per agent
- ✅ **Direct Velocity Steering** - Instant direction changes for responsive autonomous behavior
- ✅ **Patrol Behavior** - Active wandering with random destinations when idle
- ✅ **Sticky Decision Making** - Consistent choices (flee-to-hero vs panic) prevent flickering

#### Phase 4.5: Tactical Combat & Environment
- ✅ **Group Behaviors** - Zombie hordes (0.3 cohesion), civilian flee-to-hero (30%), hero squads
- ✅ **Ranged Combat System** - 100px range, variable aim delay (0.3-0.6s), 1.5s cooldown
- ✅ **Visual Shooting Effects** - Yellow gunshot lines with fade-out, 0.15s lifetime
- ✅ **Gunshot Attraction** - Zombies converge on sounds (300px radius, 3s duration)
- ✅ **Hero Personalities** - 50/50 hunter (chase 55 speed) vs defender (kite 45 speed)
- ✅ **Environment System** - 8 procedural buildings (80-150px), 30 trees (15-25px radius)
- ✅ **Graveyard Origin** - Dark purple zone (200×200px) with 8 tombstones marking outbreak start
- ✅ **Hard Collision Physics** - Pre-check movement, push-out mechanics, tangential deflection for sliding
- ✅ **Zombie Health System** - 3 hits to kill, hero exhaustion after 5 kills → zombie conversion
- ✅ **Logical Spawning** - Civilians near buildings, zombies in graveyard, heroes at strategic positions
- ✅ **Speed Balancing** - 33% global reduction (40/35/50 base speeds) for tactical gameplay

#### Quality of Life Features
- ✅ **Pause Control** - Spacebar to pause/resume, starts paused for setup
- ✅ **Resizable Window** - 1600×1200 default, drag to resize to any size
- ✅ **Camera Controls** - Zoom (0.125x-8x), pan, and reset for detailed observation
- ✅ **Time Scale Control** - Adjust simulation speed (0.125x-4x) with keyboard/slider
- ✅ **Dynamic Agent Count** - Live adjustment from 100-10,000 agents
- ✅ **macOS Retina Support** - FLAG_WINDOW_HIGHDPI for crisp display
- ✅ **Cross-Platform** - Windows/macOS/Linux support with platform-specific fixes

### Technical Highlights

**Architecture:** Decoupled simulation (60 TPS) and presentation (variable FPS) layers  
**Memory Layout:** Hot data (position, velocity, type, health) in separate contiguous arrays  
**Spatial Partitioning:** Uniform grid hash enables O(n) neighbor queries instead of O(n²)  
**Determinism:** Fixed timestep ensures reproducible results (with time scale control)  
**Rendering:** Alpha blending with wrap detection for smooth sub-frame interpolation  
**AI System:** Parallel behavior updates with type-specific seek/flee logic

## 🧟 Tactical Zombie Survival Simulation

A complex outbreak scenario with emergent tactical gameplay from simple agent rules. Zombies spawn in the graveyard, civilians defend their homes, and heroes mount organized resistance.

### Agent Types & AI States

**Civilians (White/Light Gray)** - 90% of initial population
- **AI States:** Idle → Patrol (wander) → Fleeing (panic or seek hero)
- **Behavior:** 30% flee to nearest hero, 70% panic flee (sticky decision)
- **Speed:** 40 px/s base, 45 px/s when panicking
- **Spawn:** Near buildings (home defense)
- **Death:** Convert to 3-health zombie on contact

**Zombies (Green)** - 5% of initial population  
- **AI States:** Patrol (wander) → Pursuing (chase) → Searching (last-seen)
- **Behavior:** Horde cohesion (0.3), gunshot attraction (300px, 3s), sprint when close
- **Speed:** 35 px/s patrol, 45 px/s sprint (<30px to target)
- **Health:** 3 hits to kill (ranged or melee)
- **Spawn:** Graveyard (200×200px dark zone, bottom-left)

**Heroes (Blue Gradient)** - 5% of initial population
- **AI States:** Patrol (wander) → Pursuing (hunt/kite) → Searching (investigate)
- **Personalities:** 50% Hunter (chase 55 speed), 50% Defender (kite 45 speed <70px)
- **Combat:** Ranged shooting (100px, 0.3-0.6s aim, 1.5s cooldown), melee backup
- **Exhaustion:** After 5 kills → convert to 3-health zombie
- **Spawn:** Strategic top positions for defense

### Combat Systems

- **Ranged Combat** - Heroes shoot at 100px range with variable aim delay
- **Visual Feedback** - Yellow gunshot lines (0.8px, 0.15s fade)
- **Gunshot Attraction** - Zombies hear shots and converge on location
- **Zombie Health** - 3 hits required, tracks damage per zombie
- **Melee Combat** - Close-range (15px) as backup for heroes
- **Hero Exhaustion** - Kill counter tracks fatigue, eventual conversion

### Environment & Physics

- **Buildings** - 8 procedural structures (80-150px) for civilian spawn
- **Trees** - 30 scattered (15-25px radius) creating forest obstacles  
- **Graveyard** - Dark purple zone with 8 tombstones marking outbreak origin
- **Hard Collision** - Agents cannot phase through obstacles
- **Tangential Deflection** - Slide along walls/trees naturally
- **Avoidance Forces** - 5x strength at 50px detection for steering

### Emergent Gameplay

- **Last Stands** - Heroes cornered in buildings create dramatic shootouts
- **Horde Waves** - Zombies cluster and sweep across the map
- **Civilian Tactics** - Some flee to heroes (escorts), others panic (doomed)
- **Chokepoints** - Building gaps and tree lines create tactical terrain
- **Sound Tactics** - Gunshots both kill zombies and attract more

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

### ✅ Phase 4-4.5: Tactical AI & Combat (Complete)
- ✅ Five-state AI with memory system
- ✅ Patrol, seek, flee, pursue, search behaviors
- ✅ Group behaviors (hordes, squads, flee-to-hero)
- ✅ Ranged combat with hero personalities
- ✅ Environment obstacles with hard collision
- ✅ Logical spawning and zombie health system
- ✅ Pause controls and resizable window

### 📅 Phase 5: Optimization & Scale (Planned)
- 🔄 Tracy profiler integration for deep analysis
- 🔄 SIMD optimization with __m256 vectors
- 🔄 Target: 20,000 agents @ 60 TPS
- 🔄 Health visualization (color intensity)

### 📅 Phase 6: Polish & Demo (Planned)
- 🔄 Recording system for video capture
- 🔄 Checkpoint binaries for progress showcase
- 🔄 YouTube video assembly
- 🔄 Final documentation pass

See [docs/Roadmap.md](docs/Roadmap.md) for detailed milestones.

---

## 🎮 Controls

### Simulation
- **Spacebar** - Pause/Resume (starts paused)
- **`[`** - Slow down time (0.125x, 0.25x, 0.5x, 1.0x)
- **`]`** - Speed up time (1.0x, 2.0x, 4.0x)
- **Backspace** - Reset time scale to 1.0x

### Camera
- **Mouse Wheel** - Zoom in/out (0.125x to 8x)
- **Right Click + Drag** - Pan around simulation world
- **Middle Click** - Reset camera to default view

### Window
- **Drag Edges/Corners** - Resize window (1600×1200 default)
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
