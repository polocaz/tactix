# Nation Simulator - Development Roadmap

## Milestone 0: Repository Setup
**Goal:** Establish clean project structure and build system.

**Tasks**
- [x] Create project directory and CMake setup
- [x] Add `.gitignore` for C++ / build artifacts
- [x] Add `/src`, `/include`, `/docs`, `/third_party` directories
- [x] Integrate SDL2 (or GLFW) + ImGui for windowing and UI
- [x] Add spdlog for logging
- [x] Add Tracy profiler (optional at this stage)
- [x] Build a blank window with frame counter

**Deliverable:**  
Empty project builds and displays an FPS counter in a window.

---

## Milestone 1: Core Simulation Loop
**Goal:** Implement minimal simulation loop and time-stepped updates.

**Tasks**
- [ ] Create `Simulation` class with `tick(deltaTime)` method
- [ ] Add main loop in `main.cpp` (`while (running) { sim.tick(); }`)
- [ ] Implement fixed timestep logic (e.g., 0.1s or 10 ticks/sec)
- [ ] Add `PROFILE_SCOPE("SimulationTick")` macro stub for timing
- [ ] Log basic tick rate metrics to console

**Deliverable:**  
A running tick loop that prints tick number and delta time.

---

## Milestone 2: World System
**Goal:** Implement the region-based world grid.

**Tasks**
- [ ] Create `World` class with 2D grid of `Region` structs
- [ ] Add parameters: `width`, `height`, `tileSize`
- [ ] Initialize regions with random resource & population values
- [ ] Implement `updateRegions()` method
- [ ] Expose `getRegion(x, y)` and `forEachRegion()` iterators
- [ ] Display simple world visualization (colored tiles or dots)

**Deliverable:**  
Visible map grid with simulated region data updating over time.

---

## Milestone 3: Entity System
**Goal:** Manage thousands of agents efficiently in SoA layout.

**Tasks**
- [ ] Create `Entities` structure (SoA arrays for posX, posY, state, regionId)
- [ ] Implement `spawnEntity()` and `destroyEntity()`
- [ ] Add `updateEntities()` loop (simple movement or wander behavior)
- [ ] Link entities to regions for spatial updates
- [ ] Add ImGui debug window with active entity count

**Deliverable:**  
10,000 agents moving randomly on the world map with visible metrics.

---

## Milestone 4: Job System (Parallel Execution)
**Goal:** Multithread simulation updates using a job queue.

**Tasks**
- [ ] Implement `JobSystem` with worker threads
- [ ] Create per-thread job queues and synchronization primitives
- [ ] Allow submitting lambdas (`submit([]{ ... });`)
- [ ] Parallelize entity updates by world region or chunk
- [ ] Add thread activity visualization (jobs/sec, threads used)
- [ ] Verify determinism when running single-threaded mode

**Deliverable:**  
Parallel update loop with measurable speedup on multicore CPUs.

---

## Milestone 5: Profiling & Metrics
**Goal:** Integrate real-time performance metrics.

**Tasks**
- [ ] Add `Profiler` system with timing macros (`PROFILE_SCOPE`)
- [ ] Record per-stage timing (AI, movement, world)
- [ ] Display ImGui panel for tick time graph and averages
- [ ] Integrate Tracy or output JSON logs for flamegraphs
- [ ] Track entity count, tick rate, job count in metrics

**Deliverable:**  
Visible profiler overlay + saved logs showing frame breakdowns.

---

## Milestone 6: AI and Behavior System
**Goal:** Add simple agent logic for interaction between entities.

**Tasks**
- [ ] Define `state` enum (idle, moving, gathering, fighting)
- [ ] Implement per-state behavior updates
- [ ] Add random wandering and resource gathering behaviors
- [ ] Add simple combat interaction (reduce health, despawn on death)
- [ ] Visualize state colors on the map

**Deliverable:**  
Autonomous agents performing different actions based on simple logic.

---

## Milestone 7: Visualization & Debug Tools
**Goal:** Make simulation data easily explorable.

**Tasks**
- [ ] Render entities as points or sprites
- [ ] Add ImGui controls (pause, step, speed)
- [ ] Add overlay: active agents, regions, tick time
- [ ] Add heatmap for resource/population density
- [ ] Enable screenshot/export mode for demos

**Deliverable:**  
Interactive window showing simulation metrics and visual states.

---

## Milestone 8: Large-Scale Benchmarking
**Goal:** Stress-test and document performance scaling.

**Tasks**
- [ ] Add CLI args for world size and agent count
- [ ] Run benchmarks at 1k, 5k, 10k, 50k agents
- [ ] Log tick times, thread usage, memory footprint
- [ ] Document results in `/docs/performance.md`
- [ ] Capture flamegraphs and screenshots

**Deliverable:**  
Performance report with before/after metrics and profiling visuals.

---

## Milestone 9: Polish and Documentation
**Goal:** Clean up, document, and prepare for showcase.

**Tasks**
- [ ] Refactor codebase (consistent naming, headers)
- [ ] Write `README.md` with build instructions and screenshots
- [ ] Update `DESIGN.md` with final architecture diagram
- [ ] Record short demo video (1–2 min)
- [ ] Write technical blog post: *“Scaling a Nation Simulation to 10,000 Agents in C++”*

**Deliverable:**  
Production-quality repo demonstrating simulation architecture, performance profiling, and results.

---

## Optional Stretch Goals
- [ ] Faction-level AI (ownership, war, diplomacy)
- [ ] Trade route system (resource flow between regions)
- [ ] Save/load world state
- [ ] Headless server mode for large-scale runs
- [ ] Networking hooks (future multiplayer simulation)
