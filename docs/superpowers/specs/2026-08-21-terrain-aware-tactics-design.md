# Tactix: Terrain-Aware Tactics

**Date:** 2026-08-21
**Status:** Draft design, pending implementation plan
**Scope:** Adds terrain-aware squad positioning and obstacle-safe deployment to the
medieval skirmish simulation. Builds on
[`2026-08-19-tactix-medieval-skirmish-design.md`](2026-08-19-tactix-medieval-skirmish-design.md)
without changing the performance foundation's determinism constraints.

---

## 1. Goal

Make terrain tactically meaningful instead of merely physical.

Soldiers already avoid and collide with buildings and trees, but the squad layer still
chooses objectives as if the battlefield were empty. The result is local competence and
strategic blindness: soldiers do not phase through obstacles, but squads can still
advance into blocked lanes, grind against terrain, or deploy in slots that sit on
unusable ground.

This feature has two deliverables:

1. **Deployment safety:** no soldier spawns directly on or inside an obstacle, including
   after deployment jitter.
2. **Terrain-aware positioning:** squads choose tactical approach positions around
   obstacles, preserving formation space and creating visible flanking, cover, and
   standoff behavior.

The first deliverable is a correctness fix. The second is a tactics feature.

---

## 2. Current State

Findings are from reading the tree on 2026-08-21.

### 2.1 Obstacles already exist

`Simulation::generateObstacles` creates buildings and trees with rejection sampling and
a standoff gap. Buildings and trees are stored privately in `Simulation`.

The current obstacle stack is sound enough to build on:

- `insideAnyObstacle(Vec2)` tests raw obstacle occupancy.
- `clearOfObstacles(Vec2)` slides a point outside every building and tree by
  `kObstacleStandoff`.
- `phaseSoldierSteerChunk` adds local obstacle avoidance.
- `phaseMovementChunk` performs hard collision and tangential deflection.

### 2.2 Spawn clearing is nearly right, but jitter can undo it

Deployment routes each formation slot through `clearOfObstacles`, then applies random
deployment jitter:

```cpp
const Vec2 slot = clearOfObstacles(slotWorldPosition(...));
const float px = slot.x + jx;
const float py = slot.y + jy;
soldiers.spawn(clampf(px, 0.0f, w), clampf(py, 0.0f, h), ...);
```

That means the raw slot is safe, but the final spawned point is not guaranteed safe. A
soldier standing exactly at the obstacle standoff boundary can be jittered back inside a
building or tree before spawning.

This is the first fix because every higher-level terrain feature assumes initial
positions are valid.

### 2.3 Formation targets are obstacle-cleared, not tactically chosen

`slotError` and soldier steering measure against obstacle-cleared slot positions. This
prevents a soldier from chasing an impossible point inside a wall, which is good.

It does not make the squad choose better ground. If the squad centroid advances straight
toward an enemy through a building, every soldier's local slot is adjusted one at a time.
The formation survives, but the decision was still "walk through the building."

### 2.4 Squad decisions ignore terrain

`selectTargetSquad` chooses the nearest enemy by centroid distance and sets
`SquadOrder::Advance`. The chosen facing is the vector to that enemy. There is no concept
of:

- blocked lanes
- approach points around obstacles
- archer standoff terrain
- preserving room for a formation footprint
- flanking around terrain instead of through it

### 2.5 Obstacle geometry is trapped inside `Simulation`

The functions that understand terrain live on `Simulation`, while squad decision logic
lives in `Squads.cpp`. Adding tactical terrain scoring directly to `Squads.cpp` would
either duplicate geometry or force squad code to depend on all of `Simulation`.

This feature should introduce a narrow geometry helper that both `Simulation` and squad
decision code can use.

---

## 3. Approach

Use a two-layer approach:

1. **Geometry guarantees** keep every point valid: spawn positions, formation slots, and
   tactical anchors are cleared away from obstacles.
2. **Squad-level tactical scoring** chooses useful objectives around terrain before
   soldiers steer locally.

Do not pathfind per soldier. Soldiers remain cheap: they steer to squad slots, separate
from neighbors, and avoid nearby obstacles. Terrain reasoning happens once per squad.

Three approaches were considered:

**A - Only strengthen local avoidance.** Simple, but leaves squads making bad strategic
choices. Soldiers would still appear to bump their way around decisions made on an empty
map.

**B - Full grid pathfinding immediately.** Most general, but it introduces grid build,
path caching, path following, and determinism questions before the simpler tactical
problem is solved.

**C - Tactical anchors first, pathfinding later if measured necessary.** Generate
deterministic candidate positions around obstacles and score them at the squad level.
Use local avoidance for final movement.

**Approach C is selected.** It creates visible tactical behavior with limited new state,
keeps reasoning amortized over squads, and leaves a clean upgrade path to a coarse nav
grid if anchor steering proves insufficient.

---

## 4. Terrain Geometry

### 4.1 `TerrainField`

Extract obstacle geometry into a small helper owned by `Simulation`.

```cpp
struct Building {
    float x, y, width, height;
};

struct Tree {
    float x, y, radius;
};

struct TerrainField {
    std::vector<Building> buildings;
    std::vector<Tree> trees;

    bool insideAnyObstacle(Vec2 p) const;
    Vec2 clearOfObstacles(Vec2 p) const;
    bool segmentBlocked(Vec2 a, Vec2 b) const;
    float clearanceAt(Vec2 p) const;
};
```

`Simulation` keeps ownership of generation and world lifecycle. The helper owns only
geometry operations.

`insideAnyObstacle` and `clearOfObstacles` should preserve current behavior. This is a
move of responsibility, not a retune.

### 4.2 Segment blocking

`segmentBlocked(a, b)` answers whether the straight lane between two squad-level points
crosses terrain expanded by `kObstacleStandoff`.

For buildings, use segment-vs-expanded-rectangle.

For trees, use segment-vs-expanded-circle.

This is used by squad scoring, not by per-soldier movement. It must be deterministic and
order-independent.

### 4.3 Clearance

`clearanceAt(p)` returns the distance from `p` to the nearest obstacle boundary, with
negative or zero values meaning inside an obstacle. This is not a physics value; it is a
scoring input.

Use it to prefer positions with enough room for a formation:

```cpp
requiredClearance = max(formationExtent.x, formationExtent.y) * 0.5f
```

The exact coefficient is a tuning knob. The existence of the score is not.

---

## 5. Safe Deployment

### 5.1 Clear the final spawn point

Deployment must clear after jitter:

```cpp
const Vec2 rawSlot = slotWorldPosition(squads, squadId, (uint16_t)k,
                                      squads.memberCount[squadId]);
const float jx = ...;
const float jy = ...;
const Vec2 jittered{ rawSlot.x + jx, rawSlot.y + jy };
const Vec2 clear = clearOfObstacles(jittered);

soldiers.spawn(clampf(clear.x, 0.0f, w), clampf(clear.y, 0.0f, h), ...);
```

This changes the exact initial positions for soldiers whose jitter would have placed
them inside the standoff boundary. That will move the state digest and is expected.

### 5.2 Keep deployment and steering aligned

`slotError` and `phaseSoldierSteerChunk` currently compare against a cleared raw slot.
After the spawn change, deployment starts from a cleared jittered slot while steering
still targets the cleared raw slot. That is acceptable: jitter is already treated as
initial deployment noise, and soldiers should settle to their true assigned slot.

Do not bake jitter into the persistent slot target.

### 5.3 Add public test access only if needed

`soldierX`, `soldierY`, and `insideAnyObstacle` already expose enough for deployment
tests. No new accessors are required for the spawn safety test.

---

## 6. Tactical Anchors

### 6.1 Candidate generation

For each squad decision, generate a small deterministic candidate set:

- direct advance target
- left and right flank points relative to target squad
- obstacle corner points near the direct lane
- archer standoff points near obstacle edges
- fallback hold position

Candidate count must be bounded. A target of 16 to 32 candidates per deciding squad is
enough for visible behavior without turning squad decisions into a path search.

### 6.2 Building anchors

For a building, candidate points are the four corners expanded by standoff and formation
clearance:

```text
(x - r, y - r)
(x + width + r, y - r)
(x - r, y + height + r)
(x + width + r, y + height + r)
```

Only buildings whose expanded bounds intersect the direct lane from own squad to target
squad need to contribute candidates.

### 6.3 Tree anchors

For a tree, use tangent-ish side points instead of many radial samples:

- point offset perpendicular-left from the own-to-target lane
- point offset perpendicular-right
- point on the near side
- point on the far side

Each is pushed through `clearOfObstacles`.

### 6.4 Unit-specific anchor preferences

Infantry:

- prefers clear lanes
- accepts closer positions
- values formation footprint room

Archers:

- prefers standoff positions within weapon range
- penalizes blocked line to target
- values side positions near obstacle edges

Cavalry:

- heavily penalizes tight terrain
- prefers wide flanking anchors
- avoids positions where the next lane to target is immediately blocked

These preferences should be weights in one scorer, not three separate decision systems.

---

## 7. Squad Scoring

### 7.1 Tactical objective

Add an explicit squad objective point:

```cpp
std::vector<float> objectiveX;
std::vector<float> objectiveY;
```

`targetSquad` still names the enemy. `objectiveX/Y` says where this squad is trying to
put its centroid before the next decision.

This separates "who we care about" from "where terrain says we should stand."

### 7.2 Order and facing

For `Advance`, `FlankLeft`, and `FlankRight`, facing should still point toward the
target squad, while movement slots lead toward the terrain objective.

That requires splitting two concepts that are currently coupled:

- **facing objective:** usually enemy centroid
- **movement objective:** tactical anchor or direct approach point

The current `slotWorldPosition` advances by `kAdvanceLead` along facing. For terrain
movement, the lead direction should come from the movement objective, not necessarily
from facing. Otherwise an archer could face the enemy while needing to side-step into a
clear standoff position, but the formation would keep marching forward.

Implementation option:

```cpp
std::vector<float> moveX;
std::vector<float> moveY;
```

where `moveX/Y` is normalized and used for `kAdvanceLead`; `facingX/Y` remains the
formation orientation for rendering and front/back logic.

### 7.3 Candidate score

Each candidate receives a weighted score:

```text
score =
  - distanceCost
  - blockedLanePenalty
  + clearanceBonus
  + formationRoomBonus
  + unitPreferenceBonus
  + currentObjectiveHysteresis
  + targetPressureScore
```

Definitions:

- `distanceCost`: squared or linear distance from own centroid to candidate.
- `blockedLanePenalty`: large penalty if own-to-candidate is blocked.
- `clearanceBonus`: favors ground that is not right against an obstacle.
- `formationRoomBonus`: compares clearance to this squad's formation extent.
- `unitPreferenceBonus`: archers/cavalry/infantry preferences from section 6.4.
- `currentObjectiveHysteresis`: keeps squads from switching anchors every decision.
- `targetPressureScore`: favors candidates that still move the squad toward tactical
  contact with its selected target.

Tie-break by candidate generation order. Candidate order must be stable and documented.

### 7.4 Direct lane behavior

If direct lane to the target is unblocked and the target is appropriate for the unit
type, the scorer should usually choose direct advance. Terrain awareness should not make
every squad take scenic routes.

The feature is successful when terrain changes decisions only when terrain matters.

---

## 8. Steering Integration

### 8.1 Soldier steering remains slot-based

Soldiers should not receive individual path targets. They continue to steer toward their
own formation slot.

The squad's centroid movement changes because `slotWorldPosition` uses the squad's
movement direction for the advance lead.

### 8.2 Local avoidance stays as the final guard

Obstacle avoidance and hard collision remain in the soldier phases. Tactical anchors
reduce bad objectives; they do not replace physical collision safety.

### 8.3 No cross-agent mutation

All new per-squad fields are written only by the owning squad during phase 3. Soldier
phases read those fields after the phase barrier. This preserves the existing
thread-count determinism rule.

---

## 9. Optional Phase: Coarse Navigation Grid

If tactical anchors leave squads stuck in common obstacle layouts, add a coarse
squad-level navigation grid.

Non-goals for the first implementation:

- per-soldier A*
- dynamic obstacle pathfinding around other soldiers
- continuous navmesh generation

The future grid would:

- mark cells blocked by buildings and trees expanded by standoff
- run A* only at squad decision cadence
- cache one path per squad
- expose the next path waypoint as `objectiveX/Y`

This is a fallback path, not part of the first terrain-aware implementation.

---

## 10. Rendering and Debugging

Add debug overlays after the behavior exists:

- tactical objective point per squad
- line from squad centroid to objective
- blocked direct lane highlighted when a squad chooses an anchor
- candidate anchors for one selected squad

These overlays are debugging tools. The primary visual behavior should be readable even
without them: archers take standoff ground, cavalry avoids tight terrain, and infantry
flows around obstacles rather than through them.

---

## 11. Testing

### 11.1 Deployment tests

Add:

- `deployment never spawns a soldier inside an obstacle`
- `jittered deployment is obstacle-cleared`

The first is a sweep over real `Simulation::init`. The second should construct or find a
case where a cleared raw slot plus jitter would have crossed back into the obstacle
standoff, then assert the final position is clear.

### 11.2 Geometry tests

Add direct tests for the extracted terrain helper:

- point inside building/tree is detected
- `clearOfObstacles` returns a point outside obstacles
- segment crossing an expanded building is blocked
- segment tangent outside standoff is not blocked
- segment crossing an expanded tree is blocked

### 11.3 Squad decision tests

Add small deterministic scenarios:

- direct lane clear: squad keeps direct objective
- direct lane blocked by building: squad chooses a side/corner anchor
- archer in range with blocked line: archer repositions instead of advancing into wall
- cavalry near tight terrain: cavalry prefers wider flank anchor
- identical seeds and different worker counts produce identical digest

### 11.4 Regression tests

Existing steering tests around `clearOfObstacles`, slot error, and deterministic digest
remain load-bearing. Any digest change from spawn safety or new squad fields must be
intentional and accompanied by updated baselines.

---

## 12. Risks

**Anchor scoring can look indecisive.** Without hysteresis, squads may switch from one
side of an obstacle to the other every decision tick. Objective hysteresis is required.

**Corner anchors can crowd formations.** A corner point that is clear for one soldier may
not have enough room for a full formation. Score clearance against formation extent, not
just point occupancy.

**Movement direction and facing can fight.** Terrain often asks a squad to move sideways
while still facing the enemy. This is why movement direction and facing should be split.

**Private obstacle storage can encourage duplication.** Extracting `TerrainField` early
avoids two subtly different definitions of "blocked" and "clear."

**The first version may not solve maze-like maps.** Tactical anchors are not full
pathfinding. If obstacle layouts become maze-like, the coarse navigation grid in section
9 is the intended upgrade.

---

## 13. Non-Goals

- Per-soldier pathfinding.
- Dynamic avoidance of terrain based on other squads as moving blockers.
- Destructible terrain.
- Terrain movement costs beyond blocked/open.
- Height, elevation, or true cover ballistics.
- A full navmesh.
- Rebalancing combat damage or morale as part of this feature.

---

## 14. Success Criteria

1. No soldier spawned by `Simulation::init` is inside a building or tree after jitter.
2. Formation slot steering still converges against obstacle-cleared targets.
3. Squads whose direct route is blocked choose a clear tactical objective around the
   obstacle.
4. Archers prefer standoff terrain when available and do not advance into blocked lanes
   just to approach a target.
5. Cavalry avoids tight terrain more strongly than infantry.
6. Thread-count invariance still holds for the full state digest.
7. The behavior is visible in the interactive simulation without relying on debug text:
   units flow around terrain and form on useful ground.

