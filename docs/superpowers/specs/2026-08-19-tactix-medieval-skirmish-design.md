# Tactix: Medieval Skirmish

**Date:** 2026-08-19
**Status:** Approved design, pending implementation plan
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

Three SoA arrays.

### 4.1 `SoldierHot` (~10,000 entries)

Replaces `EntityHot`. Retained unchanged: `posX`, `posY`, `velX`, `velY`, `dirX`, `dirY`.

Added:

```cpp
std::vector<uint8_t>  team;            // 0 or 1
std::vector<uint8_t>  unitType;        // Infantry | Archer | Cavalry
std::vector<uint16_t> squadId;         // index into SquadHot
std::vector<uint16_t> slotIndex;       // position within the squad's formation
std::vector<uint8_t>  health;
std::vector<float>    attackCooldown;
std::vector<uint32_t> intentTarget;    // melee target, written by the owning soldier only
std::vector<uint8_t>  intentFire;      // "loose an arrow this tick"
```

Removed: `type`, `state` (reduced to a much smaller set, see § 5.6), `lastSeenX/Y`,
`searchTimer`, `patrolTargetX/Y`, `shootCooldown`, `aimTimer`, `fleeStrategy`,
`heroType`, `reanimationTimer`, `meleeAttackCooldown`, `combatTarget`, `combatTimer`,
`combatCooldown`, `infectionTimer`, `infectionProgress`.

### 4.2 `SquadHot` (~400 entries)

```cpp
std::vector<uint8_t>  team;
std::vector<uint8_t>  unitType;
std::vector<float>    centroidX, centroidY;   // recomputed each tick
std::vector<float>    facingX, facingY;       // normalized
std::vector<uint8_t>  order;                  // see § 6.1
std::vector<uint16_t> targetSquad;
std::vector<float>    morale;                 // 0..1
std::vector<float>    discipline;             // 0..1, see § 7
std::vector<float>    baseDiscipline;         // drawn at spawn, recovery ceiling
std::vector<uint32_t> memberStart, memberCount;  // range into squadMembers
std::vector<uint32_t> decideTick;
```

### 4.3 `ProjectileHot`

```cpp
std::vector<float>   posX, posY, velX, velY;
std::vector<uint8_t> team;
std::vector<uint8_t> damage;
std::vector<float>   lifetime;
```

### 4.4 Squad membership

A separate `std::vector<uint32_t> squadMembers` holds soldier indices grouped by squad,
with `memberStart`/`memberCount` delimiting each squad's range. Rebuilt every tick by a
counting sort keyed on `squadId` (bounded by the squad count, so O(n + k)).

**Soldiers are not reordered.** Sorting the soldier arrays themselves by squad would make
each squad's members cache-contiguous, but it means permuting ~15 arrays x 10,000 entries
per tick, roughly 600 KB of shuffled writes against 40 KB for the index array.

This is recorded as an explicit experiment rather than an assumption:
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
| 2 | Squad aggregate | squads | own squad | own members' positions |
| 3 | Squad decide | squads | own squad's order/target/facing | influence grid, all squad aggregates |
| 4 | Soldier steer + intent | soldiers | own soldier | own squad's order, neighbors' positions |
| 5 | Projectile integrate + hit intent | projectiles | own projectile | spatial hash |
| 6 | Resolution | no (serial) | all | all |
| 7 | Movement integration | soldiers | own soldier | own soldier |

### 5.3 Why phase 1 is serial

The influence grid accumulates floats from 10,000 soldiers. Summed in index order on one
thread it is bit-reproducible; accumulated via atomics from workers it is not, because
floating-point addition is not associative. The grid is ~1600 cells fed by 10,000 adds,
which is cheap enough that this is not a meaningful cost.

If it later becomes one, the fix is per-worker tile accumulation followed by a merge in
fixed tile order. That is deterministic, but not worth building before it is measured.

### 5.4 Why phase 3 may read across squads

Phase 3 reads every squad's aggregate, which phase 2 wrote. This is safe only because the
barrier between them makes those aggregates read-only for the whole of phase 3. Unlike
the current `Dead`-state argument, this does not depend on which fields are involved.

### 5.5 Resolution order

Phase 6 is single-threaded and runs in a fixed order:

1. Apply melee intents, in soldier index order
2. Apply projectile hit intents, in projectile index order
3. Spawn arrows from `intentFire` flags, in soldier index order
4. Remove dead soldiers and expired projectiles
5. Update squad morale from casualties; apply officer-death discipline penalty (§ 7.3)
6. Counting-sort `squadMembers` and reassign `slotIndex`

Steps 1–3 must precede step 4, because `intentTarget` holds soldier indices that
step 4's removals invalidate.

### 5.6 State set

`AgentState`'s eight values reduce to four: `Forming`, `Engaged`, `Routing`, `Dead`.
Everything the removed states expressed (patrol, search, flee strategy, combat lock,
infection) is either gone with the scenario or has moved to the squad's `order`.

### 5.7 Digest and counters

`stateDigest()` extends to cover the new tiers, otherwise the CI thread-invariance gate
silently stops covering most of the simulation:

- Per squad: `order`, `morale`, `discipline`, `facingX/Y`, `memberCount`
- Per projectile: `posX/Y`, `velX/Y`

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
decide per tick rather than 400, and orders persist long enough to be visible.

### 6.5 Archer targeting, and why the tier exists

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
| Archer targeting | All archers fire at the squad's assigned target (concentrated volley) | Per-soldier roll; on failure the archer picks its own nearest visible enemy |
| Charge | Uniform speed, arrives as a wall | Per-soldier speed variance; the fastest arrive first and are defeated piecemeal |
| Rout threshold | Holds at much lower morale | Breaks early |
| Rally | Recovers and re-forms | Stays broken |

Each hook is a single lerp or one deterministic roll of the existing `Rng`, keyed on the
soldier index so it is reproducible and order-independent:

```cpp
rng.range(soldierIdx, RngUse::ArcherObeyVolley, 0, 100) < discipline * 100
```

Discipline also serves § 9's legibility goal: squads that visibly differ in behavior are
distinguishable on screen without any overlay.

### 7.2 The officer is formation slot 0

No stored officer index. The officer is whichever soldier currently holds `slotIndex == 0`
within the squad.

This is deliberate. A stored soldier index would need fixing up every time the resolution
phase removes a dead soldier, which is exactly the class of aliasing bug that § 2.3
documents. Slots are already reassigned by the counting sort in step 6 of § 5.5, so
succession is automatic and costs nothing.

### 7.3 Officer death

If the holder of slot 0 died during this tick's resolution, the squad's `discipline` takes
a large penalty. The squad can recover toward `baseDiscipline` over time if it survives
and rallies.

This gives a cavalry charge into a rear a consequence beyond damage: it can collapse a
unit's cohesion outright.

Officers carry additional weight in ordinary target selection, so the AI aims at them
without needing a dedicated order. An explicit "decapitation" order in the scorer is
**out of scope**, because it adds a branch and another weight set before the base scorer has
been observed running.

---

## 8. Formations and projectiles

### 8.1 Formations

A pure function with no stored state:

```cpp
Vec2 formationSlot(FormationShape shape, uint16_t slotIndex, uint32_t memberCount);
```

Returns an offset in squad-local space (right, forward), rotated by the squad's facing and
added to its centroid to give the soldier's target position.

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

### 8.2 Projectiles

On fire, the archer aims at the target's predicted position, leading it by
`distance / arrowSpeed`. The shot is then perturbed:

```
spread = baseSpread * (1 + distance/maxRange) * (1 + shooterSpeed/maxSpeed)
angle += rng.range(shooterIdx, RngUse::ArrowSpread, -spread, +spread)
```

Accuracy therefore degrades with range and with the archer moving, and a stationary archer
at close range is genuinely dangerous.

Arrow speed is ~200 px/s. At `dt = 1/60` that is ~3.3 px of travel per tick against a
~4 px agent radius, so a point hit-test per tick is sufficient.

**This is a known ceiling and gets a `ponytail:` comment naming it.** Above roughly
240 px/s, arrows begin tunnelling through targets and the hit test must become a swept
segment test.

---

## 9. Map, deployment, and rendering

### 9.1 Variable map size

`screenWidth`/`screenHeight` become genuine world dimensions that every system respects.
The hardcoded patrol bounds of § 2.1 are removed along with patrolling.

- `tactix_bench` gains `--width` and `--height` (currently hardcoded at
  `bench/main.cpp:81`)
- The interactive application gets a world size independent of window size, using the
  camera pan and zoom already present at `main.cpp:60-88`
- Influence grid cells are a fixed 100 px, so grid resolution scales with the map rather
  than being pinned to one world size

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
| `Simulation.cpp` | Tick orchestration, phase barriers, spawning, deployment |
| `Squads.cpp` | Aggregate, decide, morale, discipline, influence grid |
| `Soldiers.cpp` | Formation steering, separation, melee |
| `Projectiles.cpp` | Spawn, integrate, hit resolution |

---

## 11. Testing

The existing determinism tests survive with minimal edits: they use
`Simulation(w, h, seed, threads)`, `init`, `tick`, and `stateDigest`, none of which change
shape.

Added:

- Formation slots are stable: same squad, count, and facing yields identical offsets
- A squad flanked in its rear arc loses morale and eventually routs
- Officer death applies the discipline penalty and slot 0 is reassigned
- Arrow spread is deterministic and bounded by max range
- The counting sort produces identical `squadMembers` at any worker-thread count
- Thread-count invariance of the full digest, extended to squads and projectiles

---

## 12. Risks

**Scorer weights need tuning against observed behavior.** The weights in § 6.2 will not be
right on paper. Expect a tuning pass once it runs, driven by the overlays in § 9.3. This
is a calibration knob, not a defect.

**Phases 1 and 6 are serial, and phase 6 grows.** Resolution does considerably more than
today's `updateInfections`. If it becomes the bottleneck at 10,000 agents, the fix is
parallel intent resolution with a deterministic merge, but that is not to be built before
`tactix_bench` shows it is needed.

**Discipline touches five systems.** That is five tuning knobs introduced at once. They are
all a single lerp each, but they interact, and the tuning pass has to consider them
together.

---

## 13. Non-goals

- Distributed simulation (unchanged from the performance foundation spec)
- Behavior trees or GOAP (see § 3)
- An explicit decapitation order in the scorer (see § 7.3)
- More than three unit types
- Swept-segment projectile collision (see § 8.2)
- Terrain affecting movement cost or line of sight. Obstacles remain blockers only
- Sorting soldier arrays by squad (deferred to a measured experiment, see § 4.4)

---

## 14. Success criteria

1. 10,000 soldiers at 60 ticks/sec, measured by `tactix_bench` and published with a
   reproducing command, as the performance foundation spec requires.
2. Bit-identical state across worker-thread counts, covering squads and projectiles.
3. Armies deploy across and fight over the whole map at any configured map size.
4. Flanking a squad's rear arc measurably raises its rout probability.
5. Squad-level intent is readable on screen at zoomed-out scale.
6. `Simulation.cpp` is under 500 lines.
