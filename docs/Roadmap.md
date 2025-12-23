# Tactix - Development Roadmap

**Project Goal:** A high-performance, data-oriented simulation engine demonstrating 10,000+ autonomous agents with deterministic behavior, optimized for systems programming portfolios.

**Timeline:** 7 weeks (part-time) | **Target Platform:** Windows/macOS/Linux

> 📖 **See Also:** [Design Document.md](Design%20Document.md) for detailed architecture, algorithms, and performance budgets.

---

## Phase 1: Single-Threaded Baseline (Week 1)
**Goal:** Establish core simulation loop with fixed timestep and basic entity management.

**Performance Target:** 1,000 agents @ 60 ticks/sec

### Tasks
- [x] CMake project structure with `/src`, `/third_party`, `/docs`
- [x] Integrate SDL2 + ImGui for windowing/UI
- [x] Add spdlog for logging
- [ ] Implement fixed timestep accumulator in `main.cpp` (see Design Doc §1.1)
- [ ] Create `EntityManager` with SoA layout (Hot: pos, vel, state) (see Design Doc §2.1)
- [ ] Implement basic movement system: `posX += velX * dt`
- [ ] Add simple SDL2 renderer (draw agents as colored dots)
- [ ] Create `Simulation::tick()` orchestration method (see Design Doc §4)

**Deliverables:**
- Window displaying 1,000 moving agents
- FPS counter and basic metrics overlay
- Deterministic simulation (same seed = same output)

**Acceptance Criteria:**
- [ ] Tick time < 1.5 ms average
- [ ] No memory leaks (Valgrind/ASAN clean)
- [ ] Code compiles with `-Wall -Wextra -Werror`

---

## Phase 2: Spatial Partitioning (Week 2) ✅ COMPLETE
**Goal:** Implement uniform grid hash for efficient neighbor queries.

**Performance Target:** 5,000 agents @ 60 ticks/sec with collision detection

### Tasks
- [x] Create `SpatialHash` class with grid-based bucketing (see Design Doc §5)
- [x] Implement `hashPosition(x, y)` → cell ID (see Design Doc §5.2)
- [x] Add `entityToCell` and `cellToEntities` mappings (see Design Doc §5.3)
- [x] Implement 9-cell neighbor query (3x3 grid) (see Design Doc §5.4)
- [x] Add simple collision avoidance (steer away from nearby agents)
- [x] Visualize grid boundaries in debug mode

**Deliverables:**
- 5,000 agents with local avoidance behavior
- Debug overlay showing grid cells and agent density
- Spatial query benchmark (<100 µs per query)

**Acceptance Criteria:**
- [x] Neighbor queries check ~100-200 entities (not all 5k)
- [x] Tick time < 7.5 ms average (2-4 ms achieved, 1.5 µs per agent)
- [x] Grid update cost < 2 ms (per Design Doc §9.1 budget)

**Actual Results:**
- ✅ Tick time: 2-4 ms (well under target)
- ✅ Spatial hash rebuild: 0.5-1 ms
- ✅ Emergent flocking behavior from local interactions
- ✅ 50x performance improvement over O(n²) approach

---

## Phase 3: Job System & Parallelization (Week 3) ✅ COMPLETE
**Goal:** Implement multi-threaded worker pool with job submission.

**Performance Target:** 10,000 agents @ 60 ticks/sec (parallelized)

### Tasks
- [x] Create `JobSystem` with `std::thread` worker pool (see Design Doc §6.1)
- [x] Implement job queue with `std::mutex` + `std::condition_variable`
- [x] Add `submit(lambda)` and `waitAll()` barrier methods (see Design Doc §6.3)
- [x] Chunk entity updates into 256-agent jobs (see Design Doc §6.2 for granularity)
- [x] Parallelize movement and separation updates
- [x] Add thread utilization metrics to ImGui

**Deliverables:**
- 10,000 agents running on 7 worker threads (Apple M1/M2)
- ImGui dashboard showing jobs/frame, worker count, frame breakdown
- Speedup comparison: ~3.5x multi-thread improvement

**Acceptance Criteria:**
- [x] Tick time < 15 ms average (1.6 ms achieved, 10.7% of budget)
- [x] Speedup: 3-4x on quad-core CPU (3.5x achieved)
- [x] Thread-local neighbor buffers (no race conditions)
- [x] Memory usage < 1.7 MB (per Design Doc §9.2)

**Actual Results:**
- ✅ Tick time: 1.6 ms (well under 15ms target)
- ✅ 7 worker threads with 80 jobs/frame (40 chunks × 2 phases)
- ✅ Frame breakdown metrics: Tick 10%, Render 90% → optimized to 144 FPS
- ✅ Rendering optimized: Directional triangles show agent movement
- ✅ macOS Retina display fix applied (FLAG_WINDOW_HIGHDPI)
- ✅ Performance target exceeded: 144 FPS at 10k agents

---

## Phase 4: Utility-Based AI & State Machine (Week 4) 🔄 IN PROGRESS
**Goal:** Implement autonomous agent behaviors with state selection.

**Performance Target:** 10,000 agents with complex AI @ 60 ticks/sec

### Tasks
- [x] Define `AgentType` enum (Civilian, Zombie, Hero) (see Design Doc §7.1)
- [x] Implement seek/flee steering behaviors (civilians flee, zombies/heroes seek)
- [x] Add infection system (zombie-civilian collision converts to zombie)
- [x] Add health/damage system for hero combat (5 kills before conversion)
- [x] Color-code agents by type (white/green/blue gradient)
- [x] Population breakdown UI with real-time percentages
- [ ] Implement utility scoring functions per state (see Design Doc §7.2)
- [ ] Add `selectBestState()` logic (argmax of scores)
- [ ] Add simple resource tokens to world (agents seek them)
- [ ] State transition heatmap overlay

**Deliverables:**
- ✅ Three agent types with distinct behaviors
- ✅ Emergent outbreak patterns (infection spread, hero resistance)
- ✅ Visual feedback system (color coding, population tracking)
- 🔄 Full utility-based AI (in progress)

**Acceptance Criteria:**
- [x] Agent behaviors work correctly (flee/seek/combat)
- [x] Performance maintained with behavior updates (tick time < 5ms achieved)
- [x] Visible emergent behaviors in demo (zombie outbreak dynamics)
- [ ] AI decision cost < 5 ms (0.5 µs per agent, per Design Doc §9.1)
- [ ] State selection runs in ~20 CPU cycles per agent (per Design Doc §7.2)
- [ ] State transitions logged for replay

**Actual Results:**
- ✅ Zombie simulation with three agent types functioning
- ✅ Seek/flee behaviors creating dynamic chase/escape patterns
- ✅ Hero combat system effectively culling zombie population
- ✅ Population dynamics observable with slow-motion and camera controls
- ✅ Performance maintained: ~2-4ms tick time with behaviors

---

## Phase 5: Optimization & Profiling (Week 5-6)
**Goal:** Integrate Tracy profiler and apply micro-optimizations.

**Performance Target:** 20,000 agents @ 30 ticks/sec

### Tasks
- [ ] Integrate Tracy profiler with zone markers (see Design Doc §10.1)
- [ ] Add `PROFILE_ZONE` macros to all hot paths
- [ ] Capture baseline flamegraph (pre-optimization)
- [ ] Apply SIMD vectorization to movement loop (AVX2) (see Design Doc §14.2)
- [ ] Implement dirty region tracking for render state copy (see Design Doc §8.3)
- [ ] Add cache prefetch hints to critical loops
- [ ] Optimize memory layout (align to 64-byte cache lines)
- [ ] Run perf/VTune analysis for cache miss rates

**Deliverables:**
- Tracy session files showing frame timelines
- Flamegraph comparison (before/after)
- Technical write-up: 3+ optimizations with data

**Acceptance Criteria:**
- [ ] 20,000 agents @ 30 TPS stable (30 ms/tick)
- [ ] <4 MB hot data (fits in L3 cache, per Design Doc §9.2)
- [ ] Dirty region optimization achieves 90% copy reduction (per Design Doc §8.3)
- [ ] Documented 2-3x performance improvement with flamegraphs

---

## Phase 6: Polish & Portfolio Presentation (Week 7)
**Goal:** Finalize documentation, demo video, and showcase materials.

**Performance Target:** Production-ready codebase

### Tasks
- [ ] Refactor code (consistent naming, remove dead code)
- [ ] Add automated performance regression tests
- [ ] Write comprehensive README with architecture diagram
- [ ] Record 2-minute demo video with metrics overlay
- [ ] Create blog post: *"Building a 10k Agent Simulator"*
- [ ] Add CI/CD pipeline (GitHub Actions) for builds/tests
- [ ] Prepare 5-minute presentation slide deck

**Deliverables:**
- GitHub repository with full documentation
- Demo video posted to YouTube/portfolio site
- Performance report with benchmarks
- Interview talking points document

**Acceptance Criteria:**
- [ ] Repository builds cleanly on all platforms
- [ ] All performance targets met
- [ ] Demo showcases key technical achievements

---

## Success Metrics (Final Checklist)

### Performance
- [ ] 10,000 agents @ 60 ticks/sec (15 ms/tick)
- [ ] 20,000 agents @ 30 ticks/sec (30 ms/tick)
- [ ] Memory usage < 2 MB (hot data)
- [ ] Tick time variance < 2 ms (deterministic)

### Code Quality
- [ ] Zero warnings with `-Wall -Wextra`
- [ ] ASAN/ThreadSanitizer clean
- [ ] Automated tests pass in CI
- [ ] Code coverage > 60%

### Documentation
- [ ] Architecture diagram in README
- [ ] Performance benchmarks documented
- [ ] 3+ optimization case studies
- [ ] Demo video with voice-over

### Interview Readiness
- [ ] Can explain fixed timestep accumulator
- [ ] Can walk through SoA vs AoS tradeoffs
- [ ] Can discuss job system design choices
- [ ] Can show flamegraphs and explain bottlenecks

---

## Stretch Goals (Post-MVP)

### Advanced Features
- [ ] Influence map system for territorial AI
- [ ] Faction/alliance dynamics
- [ ] Replay recording/playback system
- [ ] Networked state synchronization (lockstep)

### Performance
- [ ] SIMD-optimized pathfinding (JPS+)
- [ ] GPU compute shader for spatial hash
- [ ] 50,000 agents @ 20 ticks/sec

### Tooling
- [ ] Custom memory allocator
- [ ] Entity inspector UI (click agent → view state)
- [ ] Scenario editor (place agents, resources)
- [ ] Python bindings for scripting

---

## Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| Concurrency bugs | High | Start single-threaded, add parallelism last |
| Scope creep | Medium | Strict milestone gates, defer stretch goals |
| Cache thrashing | High | Profile early, iterate on data layout |
| Platform compatibility | Low | Test on Windows/Linux/Mac from day 1 |
| Tracy integration issues | Low | Have fallback custom timer macros |

---

## Resource Links

- **Tracy Profiler:** https://github.com/wolfpld/tracy
- **Data-Oriented Design:** Mike Acton's CppCon talks
- **Fixed Timestep:** Gaffer on Games article
- **Job Systems:** Naughty Dog GDC presentations
- **Utility AI:** Dave Mark GDC talks
