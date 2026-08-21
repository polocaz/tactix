# Tactix: Medieval Skirmish

**Date:** 2026-08-19
**Status:** Approved design, revised after review, pending implementation plan
**Scope:** Replaces the zombie scenario with a two-army medieval skirmish built on a
squad tactical layer. The performance foundation from
[`2026-08-19-tactix-performance-foundation-design.md`](2026-08-19-tactix-performance-foundation-design.md)
is retained unchanged.

---

## 1. Goal

Replace the current scenario with two symmetric armies whose squads make tactical
decisions (formations, flanking, charges, routing) while keeping the project's stated
purpose intact: **the scenario exists to put pressure on memory layout, spatial queries,
and parallelism.**

The medieval scenario is chosen over improving the zombie one because it serves that
purpose better:

- Symmetric teams reduce to a team-id field rather than three types with divergent
  hand-written behavior.
- Formations require a squad tier, which is a second SoA array with a different size
  and access pattern from the agent array.
- Flanking requires a coarse influence map, which is a second spatial structure.
- Arrows require a third array with its own lifetime and broadphase consumption.
- Routing gives flanking a consequence, which is what makes the tactics visible.

Every one of those is a data-layout or parallelism problem. The asymmetric
civilian/zombie/hero split produced the opposite: special-case branching with nothing
underneath it.

---

## 2. Current state

Findings are from reading the tree at commit `22533ae`.

### 2.1 Agents cannot use the map

The world is 1280x720 (`main.cpp:13`), but every patrol destination is drawn from a
1800x1000 box:

- `Simulation.hpp:100-101`: `SpawnPatrolX` over `50..1850`, `SpawnPatrolY` over `50..1030`
- `Simulation.cpp:1069-1070`: `PatrolRetargetX/Y` over the same ranges

Roughly half of all patrol targets are off the right edge and a third are below the
bottom edge. Combined with the 100px wall-avoidance blend at `Simulation.cpp:1102-1155`,
agents accumulate against the right and bottom boundaries and the upper-left of the map
stays empty. This is not an AI deficiency; it is two wrong literals.

The medieval design removes patrolling entirely, so this is fixed by deletion rather
than by correcting the bounds.

### 2.2 There is no group

Every behavior is a per-agent reflex keyed on `AgentType`. No structure in the data
model can represent a unit, so "agents work together" has nowhere to live. The closest
existing approximation is hero squad cohesion at `Simulation.cpp:1030-1040`, which
averages nearby hero positions and steers toward the mean. That produces clustering, not
a formation: there is no facing, no slot assignment, and no shared decision.

### 2.3 Ranged combat is a placeholder

`Simulation.cpp:224-270` recovers "who shot whom" by reading agent indices back out of
`lastSeenX` and `lastSeenY`, the same two fields the memory and search behaviors use for
world positions. It detects that a shot occurred by testing `shootCooldown > 1.45f`
against the 1.5f value written at `Simulation.cpp:1016`.

Consequences:

- A hero that is shooting has no usable memory, because the fields are aliased.
- Shots are instantaneous, always hit, and cannot miss.
- Range is capped at 100px (`Simulation.cpp:1015`), below the 150px detection radius, so
  a ranged unit can never outrange its own perception.

There is no projectile representation of any kind. Travel time and accuracy cannot be
added to this; it needs a projectile array.

### 2.4 What is sound and stays

`SpatialHash`, `JobSystem`, `Rng`, `StateDigest`, `WorkCounters`, `DetMath`, the
`tactix_bench` harness, and the determinism tests are all scenario-agnostic and are
retained without modification to their structure. These are the parts the project's
performance claims rest on.

### 2.5 File size

`Simulation.cpp` is 1605 lines because every scenario rule landed in it. The pivot is
the point at which to split it rather than repeat the accumulation.

---

## 3. Approach

A two-tier simulation: a squad tier that makes decisions, and a soldier tier that
executes them.

Approximately 400 squads (200 per army, ~25 soldiers each) run the expensive tactical
reasoning once per squad. Ten thousand soldiers run cheap steering and local combat. The
alternatives considered and rejected:

**Single tier with a squad id on each agent.** Squad aggregates computed by reduction,
each soldier reasons for itself with squad context available. Rejected: 10,000 agents
redundantly deriving the same tactical assessment is 25x the work for the same answer,
and without a canonical slot assignment formations degrade into the loose clustering
that 2.2 already describes.

**Behavior trees or GOAP per squad.** Most expressive and the most defensible on paper.
Rejected for now: node graphs are pointer-chasing, which works against the data-oriented
thesis, and making a planner reproducible across worker-thread counts is substantial
work. Revisit only if the hand-written scorer demonstrably runs out of road.

---

## 4. Data layout

Three SoA arrays plus one grid.

### 4.1 `SoldierHot` (~10,000 entries)

Replaces `EntityHot`. Retained unchanged: `posX`, `posY`, `velX`, `velY`, `dirX`, `dirY`.

Added:

```cpp
std::vector<uint8_t>  team;            // 0 or 1
std::vector<uint8_t>  unitType;        // Infantry | Archer | Cavalry
std::vector<uint8_t>  state;           // Forming | Engaged | Routing | Dead (see 5.6)
std::vector<uint16_t> squadId;         // index into SquadHot
std::vector<uint16_t> slotIndex;       // position within the squad's formation
std::vector<uint8_t>  health;
std::vector<float>    attackCooldown;
std::vector<uint32_t> intentTarget;    // melee target, written by the owning soldier only
std::vector<uint8_t>  intentFire;      // "loose an arrow this tick"
```

`state` is **retained**, not removed. Its value set shrinks from eight to four (see 5.6),
but phases 4 and 6 need a per-soldier field to distinguish routing and dead soldiers, and
the digest covers it (see 5.7).

Removed: `type`, `lastSeenX/Y`, `searchTimer`, `patrolTargetX/Y`, `shootCooldown`,
`aimTimer`, `fleeStrategy`, `heroType`, `reanimationTimer`, `meleeAttackCooldown`,
`combatTarget`, `combatTimer`, `combatCooldown`, `infectionTimer`, `infectionProgress`.

### 4.2 `SquadHot` (~400 entries)

```cpp
std::vector<uint8_t>  team;
std::vector<uint8_t>  unitType;
std::vector<float>    centroidX, centroidY;   // recomputed each tick, phase 2
std::vector<float>    facingX, facingY;       // normalized, recomputed each tick (see 6.6)
std::vector<uint8_t>  order;                  // see 6.1
std::vector<uint16_t> targetSquad;            // persists across the decide stagger
std::vector<uint32_t> targetSoldier;          // recomputed EVERY tick, phase 2 (see 6.5)
std::vector<float>    morale;                 // 0..1
std::vector<float>    discipline;             // 0..1, see 7
std::vector<float>    baseDiscipline;         // drawn at spawn, recovery ceiling
std::vector<float>    rallyTimer;             // seconds of safety accrued while Routing
std::vector<uint32_t> memberStart, memberCount;  // range into squadMembers
```

Squads are never destroyed. A wiped-out squad has `memberCount == 0` and is skipped, which
keeps `targetSquad` valid for its whole lifetime without a liveness check.

`decideTick` was in an earlier draft and has been **removed**. The modulo rule in 6.4 is
the sole decision cadence, and the one case that genuinely cannot wait for the next decide
tick (morale crossing the rout threshold) is handled in resolution instead (see 7.4).

### 4.3 `ProjectileHot`

```cpp
std::vector<float>    posX, posY, velX, velY;
std::vector<uint8_t>  team;
std::vector<uint8_t>  damage;
std::vector<float>    lifetime;
std::vector<uint32_t> intentHitTarget;  // soldier index, or UINT32_MAX for no hit
```

`intentHitTarget` is written by phase 5 and consumed by phase 6, mirroring the soldier
intent pattern. A projectile is removed when `lifetime <= 0` or when its hit intent was
applied, so no separate `expired` flag is needed.

### 4.4 `InfluenceGrid`

Scratch memory, cleared and rebuilt every tick in phase 1.

```cpp
uint32_t cellsX = ceil(worldWidth  / 100.0f);
uint32_t cellsY = ceil(worldHeight / 100.0f);
std::vector<float> strength;  // 2 floats per cell, one per team
```

Each soldier adds its unit-type strength weight to its own cell, accumulated in soldier
index order on a single thread (see 5.3). At the default 1280x720 that is 13x8 cells;
the grid scales with the map rather than being pinned to one world size.

### 4.5 Squad membership

A separate `std::vector<uint32_t> squadMembers` holds soldier indices grouped by squad,
with `memberStart`/`memberCount` delimiting each squad's range.

Rebuilt every tick by a **stable** counting sort keyed on `squadId`, with ties broken by
the soldier's previous `slotIndex`. Stability is a correctness requirement, not an
optimization: a plain counting sort leaves within-squad order dependent on wherever
soldiers happen to sit in the `SoldierHot` arrays, so when an officer dies slot 0 would
jump to an arbitrary survivor, the formation anchor would teleport, and the squad would
lurch. Stable ordering means the officer's successor is the soldier who was already
standing next to them.

**Soldiers are not reordered to group them by squad.** Sorting the soldier arrays
themselves by squad would make each squad's members cache-contiguous, but it means
permuting ~15 arrays x 10,000 entries per tick, roughly 600 KB of shuffled writes against
40 KB for the index array.

This is distinct from death compaction, which does move soldiers. See 5.5 step 6.

The grouping question is recorded as an explicit experiment rather than an assumption:
**measure whether sorting soldier arrays by squad beats the index array**, using
`tactix_bench`. The harness exists precisely to settle questions of this shape, and the
answer is a legitimate portfolio result either way.

---

## 5. Tick phases and determinism

### 5.1 The constraint

The existing thread-count-invariance guarantee rests on one rule: *a parallel phase
writes only to its own index, and reads cross-index data only where that data is
invariant for the duration of the phase.*

Today that invariant is hand-reasoned and documented in a 12-line comment at
`Simulation.cpp:857`, which argues that `AgentState::Dead` is the only safe cross-thread
read because no parallel chunk ever writes it. That reasoning is correct but fragile.
It has to be re-derived by hand every time a field is added.

This design replaces it with a structural guarantee: **no parallel phase ever mutates
another agent.** All cross-agent mutation is deferred to a single-threaded resolution
phase via intent fields written to the actor's own slot.

### 5.2 Phases

Each phase is separated by a `jobSystem.waitAll()` barrier.

| # | Phase | Parallel over | Writes | Reads |
|---|-------|---------------|--------|-------|
| 1 | Rebuild spatial hash + influence grid | no (serial) | grid, influence | soldier positions |
| 2 | Squad aggregate, facing, target soldier | squads | own squad | own members, target squad's members |
| 3 | Squad decide (staggered) | squads | own squad's order/targetSquad | influence grid, all squad aggregates |
| 4 | Soldier steer + intent | soldiers | own soldier | own squad's order, neighbors' positions |
| 5 | Projectile integrate + hit intent | projectiles | own projectile | spatial hash |
| 6 | Resolution | no (serial) | all | all |
| 7 | Movement integration | soldiers | own soldier | own soldier |

Phase 2 reads another squad's members' positions to pick `targetSoldier`. This is safe
because soldier positions are written only in phase 7 (phase 4 writes velocity and
intents, not position), so they are read-only for the whole of phase 2.

### 5.3 Why phase 1 is serial

The influence grid accumulates floats from 10,000 soldiers. Summed in index order on one
thread it is bit-reproducible; accumulated via atomics from workers it is not, because
floating-point addition is not associative. The grid is roughly 100 cells at the default
map size, fed by 10,000 adds, which is cheap enough that this is not a meaningful cost.

If it later becomes one, the fix is per-worker tile accumulation followed by a merge in
fixed tile order. That is deterministic, but not worth building before it is measured.

### 5.4 Why phase 3 may read across squads

Phase 3 reads every squad's aggregate, which phase 2 wrote. This is safe only because the
barrier between them makes those aggregates read-only for the whole of phase 3. Unlike
the current `Dead`-state argument, this does not depend on which fields are involved.

### 5.5 Resolution order

Phase 6 is single-threaded and runs in a fixed order. The ordering is load-bearing, so
each step notes what it depends on.

1. **Apply melee intents**, in soldier index order. Both attacker and target must have
   `health > 0`; otherwise the intent is dropped. Overkill is therefore wasted rather
   than carried over, which is deterministic and also means a ragged low-discipline
   charge wastes fewer attacks than a synchronized one.
2. **Apply projectile hit intents**, in projectile index order, with the same
   `health > 0` guard on the target.
3. **Spawn arrows** from `intentFire` flags, in soldier index order.
4. **Record casualties**, before anything is removed. For each soldier that reached
   `health == 0` this tick: set `state = Dead`, increment `casualties[squadId]`, and if
   its `slotIndex == 0`, set `officerDied[squadId]`. This step exists specifically so
   officer identity is captured while the corpse still holds its slot; step 6 destroys
   that evidence.
5. **Update squad morale and discipline** from `casualties[]`, `officerDied[]`, and
   rear-arc threat. Apply rout and rally transitions (see 7.4).
6. **Compact.** Remove `Dead` soldiers and spent projectiles by swap-with-back. This
   invalidates soldier indices, which is safe only because every step that reads a stored
   soldier index (1, 2, 3, 4) has already run, and `squadMembers` is rebuilt in step 7.
7. **Stable counting-sort `squadMembers`** by `squadId`, ties broken by previous
   `slotIndex` (see 4.5). Reassign each soldier's `slotIndex` from its position within
   its squad's range.

`casualties[]` and `officerDied[]` are per-squad scratch buffers cleared at the start of
phase 6.

Steps 1 to 3 must precede step 6, because `intentTarget` and `intentHitTarget` hold
soldier indices that step 6's compaction invalidates.

### 5.6 State set

`AgentState`'s eight values reduce to four: `Forming`, `Engaged`, `Routing`, `Dead`.
The field is retained on `SoldierHot` (see 4.1). Everything the removed states expressed
(patrol, search, flee strategy, combat lock, infection) is either gone with the scenario
or has moved to the squad's `order`.

### 5.7 Digest and counters

`stateDigest()` extends to cover the new tiers and the new soldier fields. Omitting
`health` or the membership fields would let the thread-invariance gate pass while combat
silently diverged, which is the exact failure the gate exists to catch.

- Per soldier, added to the existing position and velocity mixing: `team`, `unitType`,
  `state`, `squadId`, `slotIndex`, `health`
- Per squad: `order`, `morale`, `discipline`, `facingX/Y`, `targetSquad`, `memberCount`
- Per projectile: `posX/Y`, `velX/Y`, `team`, `lifetime`

`WorkCounters` gains `squadDecisions` and `projectileHitTests`, so the work-counter CI
gate keeps its meaning.

---

## 6. The squad decision layer

### 6.1 Orders

`Hold`, `Advance`, `FlankLeft`, `FlankRight`, `Charge`, `Withdraw`, `Rout`.

### 6.2 Scoring

Each order is scored by a weighted sum and the argmax is taken. Inputs:

- Own strength: member count weighted by unit type
- Own morale and discipline
- Nearest enemy squad: centroid, facing, strength
- Local force balance sampled from the influence grid
- Rear-arc threat: whether an enemy squad lies within our rear arc, that is, whether
  *we* are the ones being flanked

### 6.3 Hysteresis

The currently-held order receives a score bonus. Without it, squads alternate between
`Advance` and `FlankLeft` on consecutive ticks and the battle reads as noise. This is a
legibility requirement, not a performance one.

### 6.4 Staggered re-decision

A squad re-scores only when `tickNumber % 15 == squadId % 15`. Approximately 27 squads
decide per tick rather than 400, and orders persist long enough to be visible. At 60 TPS
the worst-case reaction delay is 0.25 seconds.

Routing is the one transition that cannot wait for the next decide tick, and it is
applied in resolution instead (see 7.4).

### 6.5 Target selection

`targetSquad` is chosen by the scorer and persists across the 15-tick stagger, which is
safe because squads are never destroyed (see 4.2).

`targetSoldier` **must not** persist that way, because compaction reassigns soldier
indices every tick. It is recomputed for every squad, every tick, in phase 2: the member
of `targetSquad` with the lowest `slotIndex` that lies within weapon range of our
centroid. If `targetSquad` has no members in range, `targetSoldier` is `UINT32_MAX`.

Selecting the lowest `slotIndex` is what gives 7.3 its "officers are worth more" property
without a special case: slot 0 is the officer, so a squad naturally shoots at the enemy
officer when one is in range.

### 6.6 Squad facing

Facing is derived from the order's objective, not from soldier velocities:

| Order | Facing |
|-------|--------|
| `Advance`, `Charge`, `FlankLeft`, `FlankRight` | Normalized vector from own centroid to `targetSquad`'s centroid |
| `Withdraw`, `Rout` | Normalized vector away from the nearest enemy squad's centroid |
| `Hold` | Retained from the previous tick |

Averaging soldier `dirX/dirY` was considered and rejected: a low-discipline mob produces
noisy facing, and formation slots are rotated by facing (see 8.1), so the noise would
propagate into every slot position. Centroid velocity was also rejected because it is
undefined for a stationary squad.

Facing is initialized at deployment and never becomes undefined, since the `Hold` case
falls back to the previous value.

### 6.7 Archer range, and why the tier exists

Archer range is ~280px. A soldier's own neighbor query reaches 150px (`seekRadius`,
`Simulation.cpp:651`). An archer therefore cannot perceive its own best target.

Target assignment comes from the squad, which sees the field through the influence grid
and enemy squad centroids rather than through a neighbor query. "Outranges its own
perception" is not achievable in a single-tier design; this is the concrete
justification for the squad tier beyond cost amortization.

---

## 7. Discipline and officers

### 7.1 Discipline

One float per squad, 0..1, drawn per squad at spawn so that each army is a mix of elite
and levy rather than uniform. It governs how faithfully the squad's order survives
contact:

| Hook | High discipline | Low discipline |
|------|-----------------|----------------|
| Formation slot | Steer to the exact slot | Slot target blends toward squad centroid, a mob rather than a line |
| Archer targeting | All archers fire at `targetSoldier` (concentrated volley) | Per-soldier roll; on failure the archer picks its own nearest visible enemy |
| Charge | Uniform speed, arrives as a wall | Per-soldier speed variance; the fastest arrive first and are defeated piecemeal |
| Rout threshold | Holds at much lower morale | Breaks early |
| Rally | Recovers and re-forms | Stays broken |

Each hook is a single lerp or one deterministic roll of the existing `Rng`, keyed on the
soldier index so it is reproducible and order-independent:

```cpp
rng.range(soldierIdx, RngUse::ArcherObeyVolley, 0, 100) < (int)(discipline * 100.0f)
```

Discipline also serves the legibility goal in 9.3: squads that visibly differ in behavior
are distinguishable on screen without any overlay.

### 7.2 The officer is formation slot 0

No stored officer index. The officer is whichever soldier currently holds `slotIndex == 0`
within the squad.

This is deliberate. A stored soldier index would need fixing up every time compaction
removes a dead soldier, which is exactly the class of aliasing bug that 2.3 documents.
Slots are already reassigned in step 7 of 5.5, so succession is automatic and costs
nothing. Because that sort is stable (see 4.5), the successor is the soldier who was
already adjacent to the officer rather than an arbitrary survivor.

### 7.3 Officer death

Officer death is detected in step 4 of 5.5, before compaction destroys the evidence, and
recorded in the per-squad `officerDied[]` scratch buffer. Step 5 applies a large
discipline penalty to squads whose flag is set. The squad recovers toward
`baseDiscipline` over time if it survives and rallies.

This gives a cavalry charge into a rear a consequence beyond damage: it can collapse a
unit's cohesion outright.

Officers are preferentially targeted through the lowest-`slotIndex` rule in 6.5, so the
AI aims at them without a dedicated order. An explicit "decapitation" order in the scorer
is **out of scope**, because it adds a branch and another weight set before the base
scorer has been observed running.

### 7.4 Morale, routing, and rallying

Morale is a float 0..1 updated in step 5 of 5.5. The formula is a tuning knob, but its
inputs are fixed: casualties taken this tick as a fraction of squad size, the
`officerDied` flag, rear-arc threat presence, and a base recovery rate. Discipline scales
both the resistance to loss and the recovery rate.

Transitions are applied in resolution rather than by the scorer, so a breaking squad does
not wait up to 15 ticks to run:

- **Enter `Rout`** when `morale < routThreshold`, where
  `routThreshold = baseRoutThreshold * (1 - discipline * 0.5)`. A disciplined squad
  therefore holds at a morale a routing levy would have broken at.
- **Exit `Rout`** when `morale >= rallyThreshold` and no enemy squad has been within
  `rallyRadius` for `rallyDuration` seconds, tracked in `rallyTimer`. The squad
  transitions to `Hold` and resumes normal scoring.
- `rallyThreshold > routThreshold`. The gap is hysteresis: without it a squad hovering at
  the threshold would oscillate between routing and holding every tick.

Without an explicit exit, `Rout` would be a terminal state and a routed squad would run
until it left the map.

---

## 8. Formations and projectiles

### 8.1 Formations

A pure function with no stored state:

```cpp
Vec2 formationSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount);
```

Returns an offset in squad-local space (right, forward), rotated by the squad's facing
(see 6.6) and added to its centroid to give the soldier's target position.

Four shapes:

| Shape | Used by | Description |
|-------|---------|-------------|
| Line | Infantry | Wide, shallow; the shield wall |
| Column | Any, when marching | Narrow, deep |
| Wedge | Cavalry | The charge |
| Loose | Archers | Wide spacing, so a volley does not wipe the unit |

Soldier steering is: steer toward the assigned slot, plus the existing separation force,
plus engage anything within melee reach. The separation and obstacle-avoidance code at
`Simulation.cpp:367-474` is scenario-agnostic and is retained.

### 8.2 Unit stats

Indexed by `unitType`, a constexpr table of three entries.

| Unit | Speed (px/s) | Range (px) | Notes |
|------|--------------|------------|-------|
| Infantry | 45 | melee | Braced against cavalry |
| Archer | 42 | 280 | Fragile in melee |
| Cavalry | 95 | melee | Devastating from the rear, breaks on a spear line |

Arrow speed is 200 px/s.

### 8.3 Projectiles

On fire, the archer aims at `targetSoldier`'s predicted position, leading it by
`distance / arrowSpeed`. The shot is then perturbed.

**Spread is expressed in integer milliradians**, because `Rng::range` is integer-only
(`int range(uint32_t, RngUse, int lo, int hi)`, `Rng.hpp`). An earlier draft passed float
bounds to it, which does not compile and would have silently truncated if it did:

```cpp
int spreadMrad = (int)(baseSpreadMrad
                     * (1.0f + distance / maxRange)
                     * (1.0f + shooterSpeed / maxSpeed));
int offsetMrad = rng.range(shooterIdx, RngUse::ArrowSpread, -spreadMrad, spreadMrad);
```

The angle offset is applied with `detmath::sin` and its cosine counterpart, keeping the
perturbation inside the portable-transcendental path that `DetMath.hpp` exists to
provide. Accuracy therefore degrades with range and with the archer moving, and a
stationary archer at close range is genuinely dangerous.

### 8.4 Hit testing uses a swept segment, not a point test

An earlier draft specified a per-tick point test, on the grounds that a 200 px/s arrow
moves ~3.3 px per tick against a ~4 px agent radius.

That reasoning was wrong, because **tunneling depends on relative velocity, not arrow
speed alone.** Cavalry at 95 px/s crossing an arrow's path gives a worst-case relative
displacement of `(200 + 95) / 60 = 4.9 px` per tick against a 4 px radius, so arrows can
already pass through a fast-crossing target at the speeds this design specifies.

Hit testing is therefore a segment-versus-circle intersection between the arrow's
previous and current position, which is a handful of lines more than the point test and
correct at the speeds actually in use. The point test is not a viable starting point
here; it would produce arrows that visibly pass through galloping cavalry.

---

## 9. Map, deployment, and rendering

### 9.1 Variable map size

`screenWidth`/`screenHeight` become genuine world dimensions that every system respects.
The hardcoded patrol bounds of 2.1 are removed along with patrolling.

- `tactix_bench` gains `--width` and `--height` (currently hardcoded at
  `bench/main.cpp:81`)
- The interactive application gets a world size independent of window size, using the
  camera pan and zoom already present at `main.cpp:60-88`
- Influence grid cells are a fixed 100 px, so grid resolution scales with the map (see
  4.4)

### 9.2 Deployment

Armies deploy on opposite edges in formation, facing each other, with terrain scattered
between. Full-map usage becomes a property of the initial conditions rather than
something the AI has to be coaxed into.

### 9.3 Overlays

Independently toggleable layers:

- **Squad hulls**: convex hull per squad, tinted by team, alpha by morale
- **Order arrows**: one arrow per squad showing its order and target
- **Formation facing**: a tick mark on the hull
- **Influence heatmap**: the phase-1 grid, colored by team dominance
- **Projectiles**: arrows drawn as segments along their velocity

Zoomed out reads intent; zoomed in reads individual combat. The overlays double as the
debugging tool for the scorer: when a flank looks wrong, the heatmap and order arrows show
why.

---

## 10. File structure

`Simulation.cpp` (1605 lines) splits four ways:

| File | Responsibility |
|------|----------------|
| `Simulation.cpp` | Tick orchestration, phase barriers, spawning, deployment, resolution |
| `Squads.cpp` | Aggregate, facing, target selection, decide, morale, discipline, influence grid |
| `Soldiers.cpp` | Formation steering, separation, melee |
| `Projectiles.cpp` | Spawn, integrate, swept hit testing |

---

## 11. Testing

The existing determinism tests survive with minimal edits: they use
`Simulation(w, h, seed, threads)`, `init`, `tick`, and `stateDigest`, none of which change
shape.

Added:

- Formation slots are stable: same squad, count, and facing yields identical offsets
- The counting sort is stable, so officer succession goes to the previously-adjacent
  soldier rather than an arbitrary survivor
- A squad flanked in its rear arc loses morale and eventually routs
- A routed squad left unmolested rallies, and one that never gets safety does not
- Officer death sets `officerDied` before compaction and lowers discipline. The assertion
  is on a threshold and on observable behavior (the formation slot blend ratio), not on
  exact float equality, since the penalty magnitude is a tuning knob
- Melee overkill is dropped: two attackers targeting one soldier with lethal damage kill
  it once and waste the second intent
- Arrow spread is deterministic and bounded by max range
- A swept hit test catches a crossing cavalry target that a point test would miss
- Thread-count invariance of the full digest, extended to soldiers, squads and projectiles

---

## 12. Risks

**Scorer weights need tuning against observed behavior.** The weights in 6.2 and the
morale formula in 7.4 will not be right on paper. Expect a tuning pass once it runs,
driven by the overlays in 9.3. This is a calibration knob, not a defect.

**Phases 1 and 6 are serial, and phase 6 grows.** Resolution now runs seven ordered steps
and does considerably more than today's `updateInfections`. If it becomes the bottleneck
at 10,000 agents, the fix is parallel intent resolution with a deterministic merge, but
that is not to be built before `tactix_bench` shows it is needed.

**Discipline touches five systems.** That is five tuning knobs introduced at once. They
are all a single lerp each, but they interact, and the tuning pass has to consider them
together.

**Rout and rally thresholds interact with the decide stagger.** Rout is applied in
resolution and rally is gated on a timer, so a squad can change state between decide
ticks. The scorer must tolerate being entered with an order it did not choose.

---

## 13. Non-goals

- Distributed simulation (unchanged from the performance foundation spec)
- Behavior trees or GOAP (see 3)
- An explicit decapitation order in the scorer (see 7.3)
- More than three unit types
- Terrain affecting movement cost or line of sight; obstacles remain blockers only
- Sorting soldier arrays by squad (deferred to a measured experiment, see 4.5)

---

## 14. Success criteria

1. 10,000 soldiers at 60 ticks/sec, measured by `tactix_bench` and published with a
   reproducing command, as the performance foundation spec requires.
2. Bit-identical state across worker-thread counts, covering soldiers, squads and
   projectiles.
3. Armies deploy across and fight over the whole map at any configured map size.
4. Flanking a squad's rear arc measurably raises its rout probability.
5. Squad-level intent is readable on screen at zoomed-out scale.
6. `Simulation.cpp` is under 500 lines.
