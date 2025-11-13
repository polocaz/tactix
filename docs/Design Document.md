# Nation Simulator - Core Design Document

## Overview
**Nation Simulator** is a large-scale, data-oriented simulation of autonomous agents, settlements, and regions.  
The goal is to model the behavior and interactions of a living world at massive scale (tens of thousands of entities) with a focus on **performance, scalability, and system design**.

This project emphasizes:
- Multi-threaded simulation loops
- Data-oriented architecture (SoA/ECS hybrid)
- Real-time profiling and metrics
- Scalable world updates (region-based)
- Minimal visualization (dots, heat-maps, debug overlays)

---

## Core Goals
1. **Performance First:**  
   Build the simulation to handle 10,000+ agents at interactive tick rates (10–30 ticks/sec).

2. **Deterministic Systems:**  
   The same initial world seed should produce consistent outcomes when run single-threaded.

3. **Modular Systems:**  
   World, entity management, AI, and simulation logic should be independent subsystems with clear APIs.

4. **Profilable and Measurable:**  
   Every major update stage is instrumented for timing and statistics collection.

5. **Headless or Visual Modes:**  
   Run either as a console/headless simulation for profiling or with a minimal SDL2/ImGui interface for visualization.

---

## Core Systems

### 1. World
Responsible for storing and updating spatial data and high-level regions.

**Responsibilities:**
- Manage map tiles or region grid
- Store environmental and ownership data (terrain, resource, faction ID)
- Provide spatial queries for entities (nearest region, neighbors)
- Manage region-level update jobs

**Data Example:**
```cpp
struct Region {
    uint32_t id;
    uint32_t factionId;
    float food;
    float population;
    float resources;
    bool active;
};
````

---

### 2. Entity System

Manages all individual agents (workers, caravans, soldiers, etc.).

**Responsibilities:**

* Store entities in SoA layout
* Handle creation, destruction, and updates
* Maintain spatial indices for nearby entity lookups
* Expose entity iteration for system updates

**Data Example:**

```cpp
struct Entities {
    std::vector<float> posX, posY;
    std::vector<float> velX, velY;
    std::vector<uint8_t> state;
    std::vector<uint32_t> regionId;
};
```

---

### 3. Simulation Loop

The central loop that drives updates across all systems.

**Pipeline Example:**

1. Input / world events
2. AI / decision making
3. Movement & physics integration
4. Combat / interaction resolution
5. World state updates
6. Metrics & profiling output

**Pseudocode:**

```cpp
while (running) {
    PROFILE_SCOPE("SimulationTick");
    simulation.tick();
    renderer.draw();
}
```

Each stage can enqueue jobs to a **Job System** for parallel processing.

---

### 4. Job System

A lightweight task scheduler for running simulation work across multiple threads.

**Responsibilities:**

* Maintain a fixed worker thread pool
* Provide work-stealing queues or simple per-thread queues
* Support submitting jobs (lambdas/functions)
* Synchronize at frame barriers

**Example API:**

```cpp
JobHandle submit(std::function<void()> job);
void waitAll();
```

---

### 5. Profiling & Metrics

Real-time performance tracking and exportable data.

**Responsibilities:**

* Measure per-system timing (AI, movement, combat, etc.)
* Track total tick time, active jobs, thread load
* Integrate with external profiler (Tracy) for detailed analysis
* Visualize simple metrics in ImGui

**Example Metrics:**

| Metric          | Description                        |
| --------------- | ---------------------------------- |
| Tick Time (ms)  | Total simulation time per frame    |
| Entities        | Count of active agents             |
| Active Regions  | Count of regions updated this tick |
| CPU Utilization | Thread usage per frame             |

---

### 6. Visualization Layer

A minimal 2D renderer for debugging and presentation.

**Responsibilities:**

* Render entities as points or icons
* Render region overlays (ownership, resources)
* Display profiling overlays (tick time graph, entity count)
* Provide pause/step controls for the simulation

Optional: headless mode for profiling or batch simulation runs.

---

## Data Flow

```text
World Initialization
        ↓
Entity & Region Setup
        ↓
Main Simulation Loop
    ├── AI Decision Pass
    ├── Movement Pass
    ├── Interaction Pass
    ├── World/Region Pass
    ├── Metrics Collection
        ↓
Renderer / Output
```

Each subsystem operates independently but shares access to entity/region data via stable handles.

---

## Performance Targets

| Scenario    | Agents | Tick Rate | Target Time/Frame |
| ----------- | ------ | --------- | ----------------- |
| Baseline    | 1,000  | 30 TPS    | < 33ms            |
| Mid-scale   | 5,000  | 20 TPS    | < 50ms            |
| Large-scale | 10,000 | 10 TPS    | < 100ms           |

Memory target: **< 1KB per agent**

---

## Future Extensions

Once the core loop is solid:

* Add factions with AI goals (expand, trade, fight)
* Add trade routes and economic simulation
* Add combat outcomes and population changes
* Implement serialization / save & load
* Expand profiler to visualize per-system CPU breakdowns

---

## Folder Structure (Proposed)

```
/src
  main.cpp
  World.h / .cpp
  Entities.h / .cpp
  Simulation.h / .cpp
  JobSystem.h / .cpp
  Profiler.h / .cpp
  Renderer.h / .cpp
/include
  (public headers)
/third_party
  imgui/
  sdl2/
  tracy/
  spdlog/
/docs
  DESIGN.md
  TODO.md
```

---

## Build & Run

**Build:**

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Run:**

```bash
./nation_sim
```

Optional flags:

* `--headless`
* `--world-size=512`
* `--agents=10000`
* `--ticks=10000`
