# Tactix: Technical Design Document

**Current Status:** Phase 4.5 Complete - Tactical Survival Simulation  
**Latest:** Group behaviors, ranged combat, environment obstacles, hard collision physics  
**Performance:** 10,000 agents @ 60 TPS, ~1.6ms tick time, 144 FPS

## 1. High-Level Architecture

Tactix is a high-performance, data-oriented simulation engine demonstrating emergent tactical gameplay from 10,000+ autonomous agents with deterministic outcomes.

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

## 5\. Spatial Partitioning: Uniform Grid Hash

For efficient neighbor queries (collision, vision, interaction), we partition the world into uniform cells.

### 5.1 Grid Cell Size Selection

**Formula:** Cell size should approximate the agent's interaction radius.

```cpp
const float CELL_SIZE = 5.0f;  // Agents interact within 5 units
const uint32_t WORLD_WIDTH = 1000;
const uint32_t GRID_WIDTH = WORLD_WIDTH / CELL_SIZE;  // 200 cells
```

### 5.2 Hash Function

```cpp
inline uint32_t hashPosition(float x, float y) {
    int32_t cellX = static_cast<int32_t>(x / CELL_SIZE);
    int32_t cellY = static_cast<int32_t>(y / CELL_SIZE);
    return (cellY * GRID_WIDTH) + cellX;
}
```

### 5.3 Entity → Cell Mapping

We maintain two structures:
- `std::vector<uint32_t> entityToCell` — maps entity index to cell ID
- `std::vector<std::vector<uint32_t>> cellToEntities` — lists entities per cell

**Update Strategy:**
1. During movement, check if entity crossed cell boundary
2. Only update mapping if `hashPosition(newPos) != entityToCell[idx]`
3. Batch updates to avoid repeated allocations

### 5.4 Neighbor Query

```cpp
void queryNeighbors(float x, float y, float radius, 
                    std::vector<uint32_t>& outEntities) {
    // Check 9 cells (center + 8 neighbors)
    int32_t centerX = x / CELL_SIZE;
    int32_t centerY = y / CELL_SIZE;
    
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            uint32_t cellId = ((centerY + dy) * GRID_WIDTH) + (centerX + dx);
            if (cellId < cellToEntities.size()) {
                for (uint32_t entityId : cellToEntities[cellId]) {
                    // Distance check here
                    outEntities.push_back(entityId);
                }
            }
        }
    }
}
```

**Cache Efficiency:** This approach ensures we only check ~100-200 entities per query instead of all 10,000.

---

## 6\. Job System Architecture

### 6.1 Worker Pool Design

```cpp
class JobSystem {
    std::vector<std::thread> workers;
    std::vector<LockFreeQueue<Job>> perThreadQueues;
    std::atomic<bool> running{true};
    
    uint32_t workerCount = std::thread::hardware_concurrency();
};
```

**Work Stealing:** If a thread's queue is empty, it attempts to steal from another thread's queue (reduces idle time).

### 6.2 Job Granularity

**Rule:** Each job should take 50-500µs to avoid scheduling overhead.

```cpp
// Bad: Too fine-grained (1-2µs each)
for (uint32_t i = 0; i < entityCount; ++i) {
    jobs.submit([i]{ updateEntity(i); });
}

// Good: Chunked (100µs each)
const uint32_t chunkSize = 256;
for (uint32_t start = 0; start < entityCount; start += chunkSize) {
    jobs.submit([start, chunkSize]{ 
        updateEntities(start, start + chunkSize); 
    });
}
```

### 6.3 Dependency Management

We use a **Barrier Pattern** between simulation stages:

```cpp
// Stage 1: AI decisions (parallel, read-only)
for (region : regions) {
    jobs.submit([region]{ updateAI(region); });
}
jobs.waitAll();  // Barrier

// Stage 2: Movement (parallel, writes position)
for (region : regions) {
    jobs.submit([region]{ updateMovement(region); });
}
jobs.waitAll();  // Barrier

// Stage 3: Interaction (needs position data from Stage 2)
for (region : regions) {
    jobs.submit([region]{ resolveInteractions(region); });
}
jobs.waitAll();
```

---

## 7\. Detailed Entity State Machine

Each agent cycles through discrete states. The AI system evaluates transitions every tick.

### 7.1 State Enum

```cpp
enum class EntityState : uint8_t {
    Idle        = 0,  // Standing still, low priority
    Wander      = 1,  // Moving to random location
    Seek        = 2,  // Moving toward target (resource, enemy)
    Flee        = 3,  // Escaping from threat
    Attack      = 4,  // Engaging enemy
    Gather      = 5,  // Collecting resource
    Dead        = 255
};
```

### 7.2 State Transition Logic (Utility-Based)

Instead of hard-coded FSM transitions, we score each possible state based on current context:

```cpp
float scoreIdle(Entity& e) {
    return (e.energy > 0.8f) ? 0.3f : 0.1f;
}

float scoreSeek(Entity& e) {
    if (e.nearbyResources.empty()) return 0.0f;
    return (1.0f - e.inventory) * 0.8f;  // High if low inventory
}

float scoreFlee(Entity& e) {
    if (e.nearbyEnemies.empty()) return 0.0f;
    float threat = e.nearbyEnemies.size() / 10.0f;
    float health = e.health / 100.0f;
    return threat * (1.0f - health);  // High if low health + many enemies
}

// Select highest-scoring state
EntityState selectBestState(Entity& e) {
    float scores[6] = { 
        scoreIdle(e), scoreWander(e), scoreSeek(e),
        scoreFlee(e), scoreAttack(e), scoreGather(e)
    };
    return static_cast<EntityState>(argmax(scores, 6));
}
```

**Performance:** This runs in **~20 CPU cycles** per agent (6 float comparisons).

---

## 8\. Rendering & Interpolation

Since simulation runs at fixed 60 ticks/sec but rendering may run at 144 Hz, we interpolate entity positions for smooth visuals.

### 8.1 Alpha Calculation

```cpp
float alpha = accumulator / FIXED_DT;  // Range [0, 1)

// Interpolated position for rendering
float renderX = lerp(prevPosX, currPosX, alpha);
float renderY = lerp(prevPosY, currPosY, alpha);
```

### 8.2 State Snapshot Structure

```cpp
struct RenderState {
    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<uint8_t> state;  // For color mapping
    uint32_t count;
};

RenderState stateFront;  // Read by renderer
RenderState stateBack;   // Written by simulation
```

**Memory Cost:** For 10,000 agents: `(4 + 4 + 1) * 10,000 * 2 = 180 KB` (negligible).

### 8.3 Dirty Region Optimization

Instead of copying all 10k entities every frame, we track which regions changed:

```cpp
std::bitset<256> dirtyRegions;  // 256 regions max

// During simulation
if (entity moved to new region) {
    dirtyRegions.set(oldRegionId);
    dirtyRegions.set(newRegionId);
}

// During swap
for (uint32_t i = 0; i < 256; ++i) {
    if (dirtyRegions[i]) {
        copyRegionToRenderState(i);
    }
}
dirtyRegions.reset();
```

**Measured Benefit:** Reduces copy cost from 180 KB → ~20 KB per frame (90% reduction for typical scenarios).

---

## 9\. Melee Combat System

The combat system creates engaging, visually interesting encounters with emergent tactical formations.

### 9.1 Combat States

```cpp
enum class AgentState : uint8_t {
    Idle        = 0,
    Patrol      = 1,
    Fleeing     = 2,
    Pursuing    = 3,
    Searching   = 4,
    Dead        = 5,    // Corpse waiting to reanimate
    Fighting    = 6,    // Locked in melee struggle
    Bitten      = 7     // Infected, dying slowly
};
```

### 9.2 Combat Engagement Flow

**Phase 1: Initiation**
- When zombie catches human (< 15px), both enter `Fighting` state
- Agents lock onto each other via `combatTarget` field
- Combat duration: 2-4 seconds (random)
- Both agents orbit their midpoint with random jitter

**Phase 2: Struggle Visuals**
```cpp
// Calculate wiggle movement (simulates wrestling)
float midX = (agentX + targetX) / 2.0f;
float midY = (agentY + targetY) / 2.0f;
float angle = combatTimer * 3.0f;  // Rotate during struggle
float jitterX = sin(angle) * 5.0f + randomNoise(-2, 2);
float jitterY = cos(angle) * 5.0f + randomNoise(-2, 2);

// Orbit around midpoint
agentX = midX + cos(angle) * 8.0f + jitterX;
agentY = midY + sin(angle) * 8.0f + jitterY;
```

**Phase 3: Resolution (when timer expires)**

**Civilian vs Zombie:**
| Outcome | Base Chance | With 1 Ally | With 2+ Allies | Result |
|---------|-------------|-------------|----------------|--------|
| Kill Zombie | 20% | 35% | 50% | Civilian wins, zombie dies |
| Bitten & Escape | 35% | 40% | 35% | Breaks free but infected |
| Killed | 45% | 25% | 15% | Dies → corpse → reanimates in 3-8s |

**Group Combat Bonus:**
```cpp
int nearbyAllies = countAlliesInRadius(pos, 50.0f);
float survivalBonus = std::min(0.3f, nearbyAllies * 0.15f);
float killChance = 0.20f + survivalBonus;
```

**Hero vs Zombie:**
- **Win:** 80% (kills zombie instantly)
- **Damage:** 20% (hero loses 1 health, zombie takes 1 damage)
- Combat duration: 1-2 seconds (heroes are trained fighters)

### 9.3 Bitten State Mechanics

When civilian escapes but is bitten:
```cpp
entities.state[i] = AgentState::Bitten;
entities.infectionTimer[i] = 5.0f + random(0, 10.0f);  // 5-15 seconds to death
entities.infectionProgress[i] = 0.0f;
```

**Visual Progression:**
```cpp
// Color shifts from white → yellow → green as infection progresses
float progress = 1.0f - (infectionTimer / maxInfectionTime);
Color agentColor = lerp(
    Color{220, 220, 220, 255},  // Healthy white
    Color{150, 200, 100, 255},  // Sickly green
    progress
);
```

**Behavior:**
- Can still flee and move (desperate escape)
- Movement speed reduces: 40 → 30 → 20 as infection progresses
- Eventually collapses → Dead state → reanimates as zombie

### 9.4 Emergent Formation Tactics

The group combat bonus naturally encourages tactical clustering:

**Lone Civilian:**
- 45% death rate → high motivation to find others
- Flee toward nearest group of humans

**Small Group (2-3):**
- 25% death rate → safer but not invincible
- Natural defensive clustering emerges

**Large Group (4+):**
- 15% death rate → "safety in numbers"
- Zombies struggle to break through
- Resembles medieval shield wall formations

**Implementation:**
```cpp
// Civilians adjust flee direction toward allies
if (fleeStrategy == FleeToGroup) {
    Vector2 groupCenter = calculateNearbyHumansCentroid(pos, 100.0f);
    fleeDir = lerp(fleeDir, normalize(groupCenter - pos), 0.4f);
}
```

### 9.5 Corpse Mechanics

**Death → Corpse:**
- Agent enters `Dead` state
- Velocity → 0, rendered as dark red circle
- `reanimationTimer = 3.0 + random(0, 5.0)`

**Corpse Feeding (Zombie Health Regen):**
- Injured zombies (health < 3) seek corpses
- Feed range: 20px
- Consume corpse → +1 health
- Creates resource competition among zombies

**Reanimation:**
```cpp
if (state == Dead && type == Civilian) {
    reanimationTimer -= dt;
    if (reanimationTimer <= 0.0f) {
        type = Zombie;
        state = Patrol;
        health = 3;
        // Twitch animation: small random velocity
        velX = random(-20, 20);
        velY = random(-20, 20);
    }
}
```

---

## 10\. Performance Budgets & Targets

### 10.1 Per-Tick Budget (16.66ms @ 60 ticks/sec)

| Stage                  | Budget  | Per Agent |
|------------------------|---------|-----------|
| Spatial Hash Update    | 2.0 ms  | 0.2 µs    |
| AI Decision            | 5.0 ms  | 0.5 µs    |
| Movement Integration   | 3.0 ms  | 0.3 µs    |
| Interaction Resolution | 4.0 ms  | 0.4 µs    |
| Metrics/Logging        | 1.0 ms  | 0.1 µs    |
| **Total**              | **15.0 ms** | **1.5 µs** |

**Buffer:** 1.66 ms reserved for frame-to-frame variance.

### 10.2 Memory Budget

| Component              | Size per Entity | Total (10k) |
|------------------------|-----------------|-------------|
| Hot Data (pos, vel, state) | 32 bytes    | 320 KB      |
| Cold Data (name, history)   | 128 bytes   | 1.25 MB     |
| Spatial Grid Indices   | 4 bytes         | 40 KB       |
| Job System Overhead    | -               | 64 KB       |
| **Total**              | **164 bytes**   | **~1.7 MB** |

**L3 Cache Target:** Keep hot data under 4 MB to fit in typical L3 cache (6-8 MB on modern CPUs).

### 10.3 Scalability Targets

| Agent Count | Tick Time | Frames/sec | Throughput       |
|-------------|-----------|------------|------------------|
| 1,000       | 1.5 ms    | 60         | 66M ticks/sec    |
| 5,000       | 7.5 ms    | 60         | 330M ticks/sec   |
| 10,000      | 15.0 ms   | 60         | 660M ticks/sec   |
| 20,000      | 30.0 ms   | 30         | 660M ticks/sec   |

**Note:** Throughput remains constant due to parallelization.

---

## 11\. Instrumentation & Profiling Strategy

### 10.1 Tracy Integration

```cpp
#define PROFILE_ZONE(name) ZoneScoped; ZoneName(name, strlen(name))

void updateAI() {
    PROFILE_ZONE("AI_Decision");
    // ... AI logic
}
```

**Tracy captures:**
- Per-frame timelines
- Thread activity
- Memory allocations
- GPU usage (if applicable)

### 10.2 Custom Metrics Dashboard

```cpp
struct Metrics {
    float tickTime[60];     // Rolling window
    uint32_t activeEntities;
    uint32_t jobsExecuted;
    uint32_t cacheHits;
    uint32_t cacheMisses;
};
```

**ImGui Overlay:**
- Real-time tick time graph
- Entity count histogram
- Job system utilization
- Memory usage (RSS, heap)

### 10.3 Regression Testing

```cpp
// tests/perf_baseline.cpp
TEST(PerformanceRegression, TickTime_10k_Agents) {
    Simulation sim(10000);
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 600; ++i) {  // 10 seconds
        sim.tick(FIXED_DT);
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    float avgTickMs = elapsed.count() / 600.0f / 1e6;
    
    EXPECT_LT(avgTickMs, 15.0f);  // Must be under budget
}
```

---

## 12\. Implementation Phases

### Phase 1: Single-Threaded Baseline (Week 1)
- Implement EntityManager with SoA layout
- Basic movement loop (no collision)
- SDL2 rendering (dots on screen)
- **Target:** 1,000 agents @ 60 ticks/sec

### Phase 2: Spatial Partitioning (Week 2)
- Implement uniform grid hash
- Add neighbor queries
- Simple collision avoidance
- **Target:** 5,000 agents @ 60 ticks/sec

### Phase 3: Job System (Week 3)
- Implement worker pool
- Parallelize movement and AI
- Add synchronization barriers
- **Target:** 10,000 agents @ 60 ticks/sec

### Phase 4: State Machine & AI (Week 4)
- Utility-based state selection
- Resource gathering logic
- Combat interactions
- **Target:** 10,000 agents with complex behaviors

### Phase 5: Optimization & Profiling (Week 5-6)
- Tracy integration
- SIMD vectorization of hot loops
- Cache prefetching hints
- **Target:** 20,000 agents @ 30 ticks/sec

### Phase 6: Polish & Documentation (Week 7)
- ImGui debug UI
- Demo video recording
- Performance report write-up
- **Target:** Portfolio-ready repository

---

## 13\. Risk Mitigation

| Risk                          | Impact | Mitigation Strategy                              |
|-------------------------------|--------|--------------------------------------------------|
| Concurrency bugs              | High   | Start single-threaded, add parallelism gradually |
| Performance regression        | Medium | Automated perf tests in CI                       |
| Scope creep                   | Medium | Strict milestone gates, defer stretch goals      |
| Cache thrashing               | High   | Profile early, iterate on data layout            |
| Determinism issues            | Low    | Use fixed-point math or seed RNG properly        |

---

## 14\. Updated Project Structure

```text
/src
  main.cpp                // Entry point, Window creation, Accumulator loop
  /Core
    Simulation.h/.cpp     // Manages the Fixed Update loop, stage orchestration
    JobSystem.h/.cpp      // Worker thread pool, job queue, work stealing
    Logger.h              // spdlog wrapper
    Profiler.h            // Tracy integration + custom metrics
  /Data
    World.h               // Region grid data, spatial hash
    EntityManager.h/.cpp  // SoA Hot/Cold storage, spawn/destroy
    ComponentArrays.h     // Individual SoA structures (Positions, Velocities, etc.)
  /Systems
    MovementSys.h/.cpp    // Updates Position based on Velocity, collision
    AISys.h/.cpp          // Utility-based state selection, behavior execution
    SpatialHash.h/.cpp    // Grid-based neighbor queries
    InteractionSys.h/.cpp // Combat, resource gathering resolution
  /Render
    Renderer.h/.cpp       // SDL2/OpenGL draw calls, interpolation
    DebugUI.h/.cpp        // ImGui layouts, metrics dashboard
  /Tests
    test_performance.cpp  // Regression tests for tick time
    test_determinism.cpp  // Verify same inputs → same outputs
```

---

## 15\. Code Examples

### 14.1 Main Loop with Fixed Timestep

```cpp
int main() {
    Simulation sim(10000);  // 10k agents
    Renderer renderer;
    
    const float FIXED_DT = 1.0f / 60.0f;  // 60 ticks/sec
    float accumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    
    while (renderer.isOpen()) {
        auto currentTime = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        accumulator += frameTime;
        
        // Fixed timestep loop
        while (accumulator >= FIXED_DT) {
            sim.tick(FIXED_DT);
            accumulator -= FIXED_DT;
        }
        
        // Render with interpolation
        float alpha = accumulator / FIXED_DT;
        renderer.draw(sim.getRenderState(), alpha);
    }
    
    return 0;
}
```

### 14.2 Entity Update (SoA Style)

```cpp
void MovementSystem::update(EntityManager& entities, float dt) {
    PROFILE_ZONE("Movement");
    
    const uint32_t count = entities.getCount();
    auto& posX = entities.hot.posX;
    auto& posY = entities.hot.posY;
    auto& velX = entities.hot.velX;
    auto& velY = entities.hot.velY;
    
    // SIMD-friendly: compiler auto-vectorizes this
    for (uint32_t i = 0; i < count; ++i) {
        posX[i] += velX[i] * dt;
        posY[i] += velY[i] * dt;
        
        // Boundary wrap
        if (posX[i] > WORLD_SIZE) posX[i] -= WORLD_SIZE;
        if (posY[i] > WORLD_SIZE) posY[i] -= WORLD_SIZE;
    }
}
```

### 14.3 Job Submission for Parallel AI

```cpp
void AISystem::update(EntityManager& entities, JobSystem& jobs) {
    PROFILE_ZONE("AI_Parallel");
    
    const uint32_t count = entities.getCount();
    const uint32_t chunkSize = 256;
    
    for (uint32_t start = 0; start < count; start += chunkSize) {
        jobs.submit([this, &entities, start, chunkSize, count]() {
            uint32_t end = std::min(start + chunkSize, count);
            for (uint32_t i = start; i < end; ++i) {
                updateAgentBehavior(entities, i);
            }
        });
    }
    
    jobs.waitAll();  // Barrier before next stage
}
```

---

## 16\. Success Metrics

This project is **complete** when:

- [ ] 10,000 agents run at stable 60 ticks/sec on a mid-range CPU (Ryzen 5 / i5)
- [ ] Tracy profiler shows <15ms tick time with clear stage breakdown
- [ ] Automated performance tests pass in CI
- [ ] Demo video showcases agents with emergent behaviors
- [ ] GitHub README includes architecture diagrams and benchmark results
- [ ] Technical write-up explains 3+ specific optimizations with data

**Interview Readiness:** Able to explain any system in detail + walk through profiling methodology.
