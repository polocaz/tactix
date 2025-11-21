# Tactix: Technical Design Document

## 1\. High-Level Architecture

Tactix is a high-performance, data-oriented simulation engine designed to handle 10,000+ autonomous agents with deterministic outcomes.

The architecture decouples the **Simulation Layer** (Logic, Physics, AI) from the **Presentation Layer** (Rendering, UI).

### 1.1 The "Heartbeat": Fixed Timestep Accumulator

To ensure deterministic behavior (identical results on different hardware), the simulation advances in fixed discrete time steps ($\Delta t$), while the rendering occurs at the variable refresh rate of the monitor.

**The Algorithm:**
We maintain an `accumulator` of real-world time.

1.  Add frame time to `accumulator`.
2.  While `accumulator` $\ge$ `FIXED_DT` (e.g., 16.66ms):
      * Step Simulation (`tick()`).
      * Subtract `FIXED_DT` from `accumulator`.
3.  Render State (interpolating based on remaining `accumulator` / `FIXED_DT` for smoothness).

## 2\. Data-Oriented Memory Layout

Instead of traditional OOP objects, Tactix uses a **Structure of Arrays (SoA)** approach. This mimics the **L3 Market Data** structure, optimizing for CPU cache prefetching.

### 2.1 The "Hot" Path (Dense Arrays)

Data accessed every single tick (Movement, Physics) is packed contiguously.

```cpp
struct EntityHot {
    // 32 bytes per entity (fits 2 entities per 64-byte cache line)
    std::vector<float> posX;    // 4 bytes
    std::vector<float> posY;    // 4 bytes
    std::vector<float> velX;    // 4 bytes
    std::vector<float> velY;    // 4 bytes
    std::vector<uint32_t> id;   // 4 bytes
    std::vector<uint16_t> regionId; // 2 bytes
    std::vector<uint8_t> state;     // 1 byte
    std::vector<uint8_t> flags;     // 1 byte (padding/alignment)
};
```

### 2.2 The "Cold" Path (Sparse/Lookup)

Data accessed only during specific events (Rendering names, UI clicks, detailed inspection) is stored separately to avoid polluting the cache during the simulation loop.

```cpp
struct EntityCold {
    std::vector<std::string> name;
    std::vector<Inventory> inventory;
    std::vector<HistoryLog> history;
};
```

## 3\. Concurrency & Job System

Mirroring **Action Chunking** in robotics, we break the simulation tick into parallelizable jobs.

### 3.1 The Frame Packet (Double Buffering)

To allow the Simulation to run at full speed without waiting for VSync, and to prevent the Renderer from reading half-updated data (tearing), we use a **State Snapshot** model.

1.  **Sim Thread:** Writes to `State_Back`.
2.  **Sync Point:** At the end of a frame, `State_Back` is swapped or copied to `State_Front`.
3.  **Render Thread:** Reads strictly from `State_Front`.

**Optimization:** Since copying 10k entities is heavy, we track `DirtyRegions`—only regions that changed are copied to the render state.

## 4\. Simulation Pipeline (The "Tick")

Every `FIXED_DT`, the system executes these stages strictly in order:

1.  **Job: Spatial Hashing:** Rebuild/Update the grid lookup for where entities are.
2.  **Job: AI Decision (Parallel):** Agents read the world state and decide `NextAction`.
3.  **Job: Integration (Parallel):** Update `PosX += VelX * dt`.
4.  **Job: Interaction Resolution:** Resolve collisions or combat (Requires thread-safe writes or specific "Conflict" phase).
5.  **Metric Collection:** Push stats to Tracy/ImGui.

## 5\. Updated Project Structure

```text
/src
  main.cpp            // Entry point, Window creation, Accumulator loop
  /Core
    Simulation.cpp    // Manages the Fixed Update loop
    JobSystem.cpp     // Worker thread pool
    Logger.h          // spdlog wrapper
  /Data
    World.h           // Region grid data
    EntityManager.h   // SoA Hot/Cold storage
  /Systems
    MovementSys.cpp   // Updates Position based on Velocity
    AISys.cpp         // Updates State/Velocity based on Logic
  /Render
    Renderer.cpp      // SDL2/OpenGL draw calls
    DebugUI.cpp       // ImGui layouts
```
