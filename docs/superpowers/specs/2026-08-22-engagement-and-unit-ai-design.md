# Tactix: Engagement and Unit AI

**Date:** 2026-08-22
**Status:** Draft design, pending implementation plan
**Scope:** Replaces the melee contact model, adds an army-level coordination tier, and
gives archers positional and self-preservation behavior. Builds on
[`2026-08-19-tactix-medieval-skirmish-design.md`](2026-08-19-tactix-medieval-skirmish-design.md)
and [`2026-08-21-terrain-aware-tactics-design.md`](2026-08-21-terrain-aware-tactics-design.md),
and preserves the performance foundation's determinism constraints unchanged.

---

## 1. Goal

Make a battle look like a battle.

Three observed failures motivate this design, and all three are visible within seconds of
starting the simulation:

1. **Melee is a blender.** Two infantry squads that meet do not form a line. They
   interpenetrate, their centroids merge, and both formations orbit the shared point.
2. **Squads do not cooperate.** Every squad independently picks the nearest enemy squad.
   Nothing screens the archers, nothing spreads targets, nothing flanks together.
3. **Archers behave like slow infantry.** They advance on their target indefinitely,
   never retreat, and never consider what is between them and what they are shooting at.

The deliverables are therefore:

1. **A contact model** in which formations hold, the front rank fights, rear ranks close
   up without losing their rank, and a shaken unit visibly loses cohesion.
2. **A coordination tier** that assigns squads roles and targets so combined arms
   behavior is a guarantee rather than a tuning outcome.
3. **Archer behavior** covering firing discipline while moving, friendly-fire-aware
   positioning and target selection, screening behind friendly infantry, and flight from
   closing melee.

Failure 1 is a correctness bug with a specific root cause (section 2.1). Failures 2 and 3
are missing features.

---

## 2. Current State

Findings are from reading the tree on 2026-08-22, which includes the uncommitted
terrain-aware tactics work.

### 2.1 The spin has one cause: a centroid/slot feedback loop

Three facts combine into the observed behavior:

- `updateSquadAggregate` (`src/Squads.cpp`) sets a squad's centroid to the mean of its
  live members' positions, every tick.
- `slotWorldPosition` (`src/Soldiers.cpp`) builds every formation slot from that
  centroid, plus `kAdvanceLead` along the movement direction.
- `phaseSoldierSteerChunk` steers every soldier toward its slot.

So slots follow the mean of the positions, and the positions follow the slots. While a
squad is marching across open ground this loop is stable, because `formationMeanOffset`
makes the centroid a genuine fixed point. It stops being stable at contact, because
nothing stops the advance. Two squads walk their centroids into each other until the two
centroids very nearly coincide, at which point:

- `selectTargetSquad` derives facing from a centroid-to-centroid vector that is now near
  zero length, so facing flips direction on small numeric changes;
- formation slots are rotated by facing, so the whole formation snaps around;
- the members chase the snapped slots, moving the mean, which moves the slots again.

The result is a rotating blob. Facing is the visible symptom, the feedback loop is the
mechanism, and the absence of any contact halt is the cause.

### 2.2 There is no soldier-to-soldier collision

`phaseMovementChunk` collides soldiers with buildings and trees only. Nothing prevents
two soldiers occupying the same point. The separation force in `phaseSoldierSteerChunk`
is deliberately weak (`kSeparationRadius` is 10px at strength 300, documented in
`src/Formation.hpp` as existing only to prevent visual overlap and explicitly sized so it
never fights a held formation). It cannot and should not act as collision.

This is why rear ranks walk through their own front rank and into the enemy: nothing is
in their way.

### 2.3 Squad coordination was specced and never built

`phaseSquadDecide` and `rebuildInfluence` are stubs. `SquadOrder` has two values, `Hold`
and `Advance`. `selectTargetSquad` is nearest-enemy-centroid, evaluated independently per
squad with no awareness of what any friendly squad is doing. The seven orders, influence
grid, morale, rout, and decide stagger described in the medieval skirmish design (its
sections 6 and 7.4) are unimplemented.

`casualties` and `officerDied` are recorded every tick in `phaseResolution` and read by
nothing. They were left as the input morale would need.

### 2.4 Archers already have a moving-accuracy penalty

`spawnArrows` (`src/Projectiles.cpp`) already scales arrow spread by
`(1 + dist/maxRange) * (1 + shooterSpeed/maxSpeed)`. The mechanic exists. What is missing
is any reason for an archer to stop moving, since nothing rewards standing still and
nothing threatens an archer who advances forever.

### 2.5 Friendly fire is impossible today

`integrateProjectile` skips any neighbor on the arrow's own team. Arrows pass through
friendly bodies without effect for the whole flight. "Avoid friendly fire" is therefore
not currently a behavior an archer could have, because there is nothing to avoid.

### 2.6 What is sound and stays

- The SoA layout, the spatial hash, the job system, and the phase structure.
- The determinism contract in full: no cross-agent mutation outside serial resolution,
  fixed iteration orders, index tie-breaks, `detmath` for anything feeding the digest.
- `formationSlot`, `formationMeanOffset`, and `formationExtent` as pure functions.
- The terrain layer (`TerrainField`, `chooseTacticalObjective`) in mechanism. Its scorer
  gains terms in this design; nothing about it is replaced.
- The swept-segment projectile hit test.

---

## 3. Approach

Four layers, from the most local to the most global. Each is independently testable, and
the later ones consume state the earlier ones publish.

1. **Contact and cohesion** (section 5). Cut the feedback loop by latching a squad's
   formation anchor when its front rank makes contact. Add a non-penetration pass so
   bodies stop bodies. Compress rank depth by cohesion so a shaken unit bunches.
2. **Morale and discipline** (section 6). Turn the already-recorded casualty data into
   the cohesion and rout inputs the layers above and below both need.
3. **Army coordination** (section 7). A two-entity commander tier that assigns roles and
   targets, so screening and target spreading are structural rather than emergent.
4. **Archery** (section 8). Arrow arc, friendly-fire-aware positioning and targeting,
   firing discipline, and flight.

These four layers are also the implementation plan's stages, in this order. Layer 1 alone
fixes the most visible failure and is independently shippable; layer 2 is what layers 1
and 4 both read; layers 3 and 4 are features on top. A tuning pass against observed
behavior follows as its own stage rather than being folded into any of them.

The influence grid specced in the skirmish design is **dropped**. Its only consumers were
going to be local force balance and rear-arc threat for the squad scorer. The commander
tier computes team aggregates directly, and rear-arc threat is cheaper to derive from
squad centroids on a decide stagger than from a grid rebuilt every tick.
`rebuildInfluence` and its stub are removed.

---

## 4. Determinism and Cost Constraints

These are inherited and non-negotiable. Every mechanism below is designed against them.

- `stateDigest()` must be bit-identical across worker-thread counts. Any new parallel
  phase must have each agent write only its own state and read only data that is
  read-only for the duration of the phase.
- Cross-agent mutation happens only in serial `phaseResolution`.
- Iteration is in ascending index order, ties break on index, and anything feeding the
  digest uses `detmath` rather than libm.
- New per-agent work is O(neighbors) through the spatial hash, never O(n squared) over
  soldiers. O(squads squared) is acceptable on a decide stagger, since squad count is in
  the hundreds and the work is amortized over the stagger interval.

**Expected cost.** `phaseContact` (section 5.3) adds one neighbor query per soldier per
tick, which roughly doubles the tick's neighbor-query load. This is a known, accepted
cost, not a regression to be discovered in the benchmark. The README's published tick
figures and the files in `tests/baseline/` both move, and regenerating them is an
explicit plan step.

---

## 5. Contact and Cohesion

### 5.1 Contact detection

`SquadHot` gains `contact` (uint8) and `contactTimer` (float).

Detection runs in `phaseSquadAggregate`, which already walks each squad's members and is
already parallel across squads writing only its own squad. For each member holding rank 0,
test for a live enemy within `kContactRadius`, which is set slightly above `kMeleeReach`
so a squad registers contact just before its front rank can swing.

Two constraints on the implementation, both easy to get wrong:

- Detection may read **soldier positions** (finalized last tick, read-only for this whole
  tick) and the spatial hash, but it may **not** read any other squad's centroid. Every
  squad's centroid is being written concurrently by its own job in this same phase. This
  is the identical hazard `updateSquadAggregate`'s existing comment documents for facing,
  and it is why contact detection tests against soldiers rather than against squads.
- Rank must be derived by the same rule `formationSlot` uses for that squad's shape, not
  by a single formula. `Wedge` ranks by `floor(sqrt(slotIndex))`; `Line`, `Column`, and
  `Loose` rank by `slotIndex / rankWidth`. A shared helper next to `formationSlot` is the
  right home, so the two definitions cannot drift.

The squad is in contact when at least `kContactFraction` of its rank-0 members have such
an enemy. Using a fraction of the front rank rather than any single soldier is what stops
one over-eager skirmisher halting an entire formation.

`contactTimer` counts down from `kContactClearTicks` whenever the condition fails, and
resets while it holds. Contact clears only when the timer expires. Without that delay a
squad would flicker between engaged and advancing as individual enemies die.

### 5.2 Anchor latching

`SquadHot` gains `anchorX/anchorY`.

On the rising edge of `contact`, the squad latches `anchor = centroid`. While `contact` is
set, `slotWorldPosition` builds slots from `anchor` instead of `centroid`, and applies no
advance lead. When contact clears, the anchor eases back toward the live centroid over
`kAnchorReleaseTicks` rather than snapping, so a squad that has just won its fight does
not teleport its formation.

**This is the fix for section 2.1.** With slots anchored to a fixed point, the mean of the
member positions no longer determines where the members are told to stand, and the
feedback loop is cut. The formation holds its ground at the place it made contact, which
is also what a line of infantry actually does.

The anchor is a squad-local write in a phase that already writes squad-local state, so it
costs nothing in determinism.

### 5.3 Non-penetration

A new parallel phase, `phaseContact`, running after `phaseMovement`.

`phaseMovement` writes integrated positions into scratch arrays `nextPosX/nextPosY`
instead of into `posX/posY`. `phaseContact` then reads `nextPos*` as read-only shared
data, and each soldier writes only its own `posX/posY`:

- Query neighbors within `2 * kSoldierRadius` (8px).
- For each overlapping neighbor, accumulate a displacement of **half** the overlap along
  the separating axis. The neighbor's own job applies the other half, so the resolution is
  symmetric without either job writing the other.
- Write `pos = nextPos + accumulated displacement`.

Thread-count invariance holds by construction: every agent's displacement is a function of
read-only shared data and its own index, so it is identical regardless of how the work is
chunked. Accumulation order within one agent is fixed by the spatial hash's cell walk,
which is already documented as insertion-ordered and platform-stable.

**Radius layering.** 8px (non-penetration) sits below `kSeparationRadius` (10px) which
sits below `kSlotSpacing` (12px). A soldier standing correctly on its slot feels zero
force from any of the three. This is the same layering argument `kSeparationRadius`'s
existing comment makes, extended by one term, and it is why adding hard collision does
not break held formations.

**One pass, not solved to convergence.** Overlap bleeds off over several ticks rather
than being eliminated within one. This is deliberate: an instantly-resolved constraint
reads as a rigid body, and a gradually-resolved one reads as a press of bodies. It is
also cheaper.

**Stale hash.** The spatial hash was built from `posX/posY` at the top of the tick, so it
is one movement step out of date here. At `maxSpeed` (150 px/s) and a 60 Hz timestep that
is 2.5px against 50px cells, so a 3x3 cell query still finds every soldier within 8px.
This is a documented tolerance, not an oversight, and it saves a full hash rebuild.

### 5.4 Rank cohesion

`slotWorldPosition` scales the **forward** component of each slot offset (its rank depth)
by a compression factor. The lateral component is never scaled: a formation that narrows
under pressure would look like a funnel, not a crowd.

```
cohesion    = discipline * morale                       // 0..1, section 6
compression = lerp(kMinCompression, 1.0, cohesion)
```

An organized unit keeps full rank spacing. A shaken one collapses toward its front rank,
which is the visible "unorganized units bunch up more" behavior.

While `contact` is set, an additional `kContactCompression` factor pulls rear ranks
further forward. Men press into a fight. Ranks stay ranks, because compression scales
rank depth uniformly and never reorders slots.

### 5.5 Front-rank-only fighting requires no new rule

`selectMeleeTarget` already selects by `kMeleeReach`. Once non-penetration keeps
rear-rank soldiers physically behind their own front rank, they are outside reach of any
enemy and acquire no target. The behavior falls out of the geometry.

This is worth stating explicitly because the obvious alternative, gating melee on rank
index, would be both redundant and wrong: a soldier who ends up at the front because the
man ahead of him died should fight immediately, and a reach test gives that for free
while a rank test would not.

### 5.6 Facing slew limit

Facing currently snaps to the normalized centroid-to-centroid vector every tick. It
instead turns toward that vector at no more than `kFacingSlewRate` radians per second.

This removes the sign-flip described in section 2.1 as an independent guard, so the spin
cannot recur even if a future change reintroduces near-coincident centroids. It also
makes every formation turn read as a maneuver rather than a snap, which is a legibility
win everywhere, not only at contact.

The rotation uses `detmath`, since facing rotates every formation slot and therefore feeds
the digest.

---

## 6. Morale and Discipline

Implemented as the medieval skirmish design's section 7.4 specified, because sections
5.4, 7, and 8 all need a real number for "how shaken is this unit."

**Inputs**, all already available: casualties this tick as a fraction of squad size, the
`officerDied` flag, presence of an enemy squad in the rear arc, and a base recovery rate.
Discipline scales both resistance to loss and recovery rate. `casualties` and
`officerDied` are populated today by `recordCasualties` and read by nothing; this is the
consumer they were recorded for.

**Placement.** Morale updates in `phaseResolution` as its step 5, the slot the existing
comment reserves for it. Rout entry and exit are applied there too, not in the scorer, so
a breaking squad does not wait up to a full decide stagger to run.

**Transitions.**

- Enter `Rout` when `morale < routThreshold`, where
  `routThreshold = kBaseRoutThreshold * (1 - discipline * 0.5)`. A disciplined squad holds
  where a levy breaks.
- Exit `Rout` when `morale >= rallyThreshold` and no enemy squad has been within
  `kRallyRadius` for `kRallyDuration` seconds, tracked in a `rallyTimer`.
- `rallyThreshold > routThreshold`. The gap is hysteresis, without which a squad at the
  threshold oscillates every tick.

Without an explicit exit, `Rout` is a terminal state and a routed squad runs off the map.

**Rout overrides contact.** A routing squad clears its `contact` flag and releases its
anchor. A formation that has broken is not holding a line.

---

## 7. Army Coordination

### 7.1 The tier

New `ArmyHot`, a third SoA alongside `SquadHot` and `SoldierHot`, with exactly two
entries, one per team. It holds:

- Aggregate strength per unit type, summed from live squads.
- `frontX/frontY` and `frontDirX/frontDirY`: the army's battle line, derived from its
  infantry squad centroids and the direction to the enemy army centroid.
- `posture`: whether the army presses, holds, or falls back.

The front line is the load-bearing field. It is what makes "behind our line" a
well-defined place, which every archer positioning decision and every reserve placement
needs and which no squad can compute on its own.

### 7.2 `phaseArmyDecide`

A new **serial** phase between `phaseSquadAggregate` and `phaseSquadDecide`. Two entities
make serial execution free, and serial execution makes bit-reproducibility free. It runs
on a stagger, `tickNumber % kArmyDecideInterval == team`, so the two armies never decide
on the same tick and each holds its assignment long enough to be visible.

Placement is forced: it must run after every squad centroid is final (phase 2's barrier)
and before any squad chooses an objective (phase 4).

### 7.3 Roles

`SquadHot` gains `role` (uint8) and `wardSquad` (uint16).

| Role | Assigned to | Objective |
|---|---|---|
| `Line` | Infantry | Anchor the battle line, engage the assigned enemy squad |
| `Screen` | Infantry | Stand between `wardSquad` (a friendly archer squad) and its nearest threat |
| `Flank` | Cavalry, spare infantry | Wide route to an assigned enemy squad's flank |
| `Shoot` | Archers | Hold a firing position behind the line |
| `Reserve` | Leftovers | Hold behind the line, fill gaps |

### 7.4 Assignment

Evaluated in ascending squad index with index tie-breaks throughout, which is what makes
the result identical on every platform and thread count.

1. Every archer squad takes `Shoot`.
2. Each archer squad with an enemy melee squad inside `kScreenThreatRadius` claims at
   most one infantry squad as its `Screen`, choosing the nearest unclaimed one. The cap is
   what stops the whole army becoming bodyguards.
3. Cavalry squads take `Flank` against enemy squads whose nearest friendly support is
   farthest away, which is the cheapest available definition of an exposed flank.
4. Remaining infantry take `Line`, distributed over enemy squads by greedy lowest-load
   assignment where each enemy squad's demand is proportional to its strength.
5. Anything left takes `Reserve`.

`wardSquad` is `UINT16_MAX` when a squad holds no `Screen` assignment.

**Initial assignment.** The stagger must not leave a squad acting on a role it has never
been given. Both armies decide unconditionally on the first tick, and only afterwards
follow `tickNumber % kArmyDecideInterval == team`. Squads spawn as `Reserve` with
`wardSquad` unset, and no squad ever reads an unassigned role because the first decide
precedes the first `phaseSquadDecide`.

Step 4 is the target spreading. It is the direct answer to "squads do not coordinate":
today every squad picks the nearest enemy, so a single forward enemy squad attracts the
entire army while the rest of the enemy line advances unopposed.

**Cost.** O(ownSquads x enemySquads), roughly 40,000 operations at 200 squads per side,
run twice per `kArmyDecideInterval` ticks. Negligible against a 10,000-agent tick, and it
is why an O(squads squared) assignment is affordable where an O(soldiers squared) one
would not be.

### 7.5 What changes in the squad layer

`selectTargetSquad` stops choosing by nearest centroid and reads the commander's
assignment. `SquadOrder` grows to `Hold`, `Advance`, `Engaged`, `Flank`, `Screen`,
`Withdraw`, `Rout`. Values are **appended, never renumbered**, because order feeds the
digest.

`Engaged` is the contact-halt state from section 5.2. `Withdraw` and `Rout` differ
deliberately: a withdrawing squad keeps its formation and rallies on command, a routing
one does neither.

`chooseTacticalObjective` is unchanged in mechanism. It keeps generating and scoring
terrain candidates, but around the objective the role implies rather than always around
the direct lane to the target. Terrain awareness composes with coordination instead of
being replaced by it, which is the whole reason the terrain work was built as a scorer.

---

## 8. Archery

### 8.1 Arrow arc

`ProjectileHot` gains `traveled` and `liveAfter`. At spawn,
`liveAfter = kArrowArcFraction * distanceToTarget`. `integrateProjectile` accumulates
`traveled` and performs no hit test while `traveled < liveAfter`.

Once live, the arrow hit-tests **both teams**. The same-team skip in `integrateProjectile`
is deleted. The shooter cannot hit itself, not by a special case but because it is behind
the arm distance.

This resolves a conflict in the requirements. "Put infantry between yourself and the
target" and "avoid friendly fire" are contradictory under a flat trajectory, because your
own screen is exactly what you would be shooting through. Under an arc they are
consistent, and they are consistent for the same reason they were in reality: massed
archery was indirect, so the danger to your own side came from where the arrows landed,
not from where they were loosed.

Concretely: a friendly standing in front of the archer is under the arc and safe.
Volleying into a melee where your men are mixed with the enemy kills your men.

Two consequences worth stating so they are not mistaken for bugs. A point-blank shot has
a small `liveAfter` and so is live almost immediately, which is correct: close range
archery is direct fire, not indirect. And an arrow that misses stays live for the
remainder of its flight, so a long overshoot can still strike whatever is behind the
target, on either side.

`traveled` accumulates as a per-projectile float in a phase that already writes only its
own projectile, so determinism is unaffected.

### 8.2 Firing position

The `Shoot` role's objective extends the existing candidate scorer with two terms, rather
than introducing a second mechanism:

- **`screenBonus`**: rewards a candidate that has a friendly `Line` or `Screen` squad
  inside the corridor between the candidate and the target. This is what makes archers
  seek cover behind their own infantry.
- **`friendlyFireRisk`**: penalizes a candidate whose terminal zone (section 8.1) contains
  friendly squad centroids.

Both are evaluated at squad centroid granularity, not per soldier. A squad-tier
approximation is correct here rather than merely cheap: an archer squad decides where to
stand as a unit, and per-soldier lane tests would produce a formation that disagrees with
itself about where to be.

### 8.3 Target selection

`friendlyFireRisk` also feeds target selection. A squad whose only in-range target sits in
a melee full of its own men **holds fire** rather than volleying into its own line.

This is the half of the requirement that positioning cannot satisfy. Where you stand
controls what is in front of you; what you shoot at controls what is around the impact.

### 8.4 Firing discipline while moving

The existing spread penalty (section 2.4) gets steeper, and gains a threshold. An archer
shoots at full accuracy only after `kSteadyTime` below a walking speed, tracked per
soldier.

This is what actually changes archer behavior, and it changes it without a rule telling
archers to stop. Today, advancing forever is free. With a settle time, a squad that keeps
repositioning keeps missing, so standing still becomes worth something and holding a
firing position becomes the archer's own preference rather than an instruction.

### 8.5 Flight

A `Shoot` squad flips to `Withdraw` when an enemy melee squad on `Advance` or `Flank` is
inside `kArcherPanicRadius` and closing. It exits at `kArcherRallyRadius`, and the gap
between the two radii is hysteresis for the same reason the rally thresholds have one.

**The flip is squad-local and evaluated every tick** in `phaseSquadDecide`, not by the
commander. Roles come from the army tier on a stagger; panic cannot wait for it. This is
the same reasoning that puts rout transitions in resolution rather than in the scorer
(section 6): the commander decides what a squad is *for*, and the squad decides when it
is about to die. The role stays `Shoot` throughout, so the squad resumes shooting when it
rallies without needing a new assignment.

While withdrawing:

- Speed is multiplied by `kFleeSpeedMultiplier` in `steerToward`. Archers drop their
  discipline and run.
- The objective is away from the threat and behind the army's own front line
  (section 7.1), so fleeing archers run toward protection rather than into a corner.

**Cannot fire backward** is expressed as one condition, not a state check: an archer may
not fire at a target more than `kMaxFireAngle` off its own movement direction while moving
faster than a walk. Flight points away from the enemy, so backward fire is forbidden with
no special case for fleeing, while an archer sidestepping slowly into position can still
loose a shot. One rule, two behaviors, and no state to keep consistent.

---

## 9. Tick Phases

Two insertions and one removal. Everything else keeps its current character.

| # | Phase | Parallelism | Change |
|---|---|---|---|
| 1 | `rebuildSpatialHash` | serial | unchanged |
| 2 | `phaseSquadAggregate` | parallel over squads | gains contact detection (5.1) and anchor latching (5.2) |
| 3 | `phaseArmyDecide` | serial, 2 entities | **new** (7.2) |
| 4 | `phaseSquadDecide` | parallel over squads | reads commander assignment (7.5) |
| 5 | `phaseSoldierSteer` | parallel over soldiers | cohesion (5.4), firing discipline (8.4) |
| 6 | `phaseProjectiles` | parallel over projectiles | arrow arc (8.1) |
| 7 | `phaseResolution` | serial | gains morale and rout as step 5 (6) |
| 8 | `phaseMovement` | parallel over soldiers | writes `nextPos` instead of `pos` |
| 9 | `phaseContact` | parallel over soldiers | **new** (5.3) |
| 10 | `clampToWorld` | serial | unchanged |

`rebuildInfluence` is removed (section 3).

The barrier discipline in `tick()` is unchanged: `tick()` remains the single owner of
every barrier, and neither new phase carries one internally.

---

## 10. File Structure

**New:**

- `Army.hpp` / `Army.cpp`: `ArmyHot`, aggregate computation, role and target assignment.
- `Contact.hpp` / `Contact.cpp`: contact detection and the non-penetration pass.
- `Morale.hpp` / `Morale.cpp`: morale update and rout transitions.

**Modified:**

- `Units.hpp`: new orders, roles, and constants.
- `Squads.hpp` / `Squads.cpp`: new fields, commander-driven targeting, new scorer terms.
- `Soldiers.cpp`: anchor-relative slots, cohesion compression, flee speed.
- `Formation.hpp`: rank-depth compression parameter.
- `Projectiles.hpp` / `Projectiles.cpp`: arc, both-team hit testing, steeper spread.
- `Simulation.hpp` / `Simulation.cpp`: phase wiring, `nextPos` scratch, digest coverage.

Three new files rather than growing `Squads.cpp` past 600 lines. This follows the original
design's own file-size concern (its section 2.5) and the boundary it implies: geometry,
squad decisions, army decisions, and morale are four separable responsibilities.

---

## 11. Testing

Property tests wherever behavior is the point, golden values only where determinism is.

### 11.1 Contact and cohesion

- Two infantry squads advance into each other: neither centroid passes through the other,
  and the distance between them stabilizes instead of orbiting. This is the regression
  test for section 2.1 specifically.
- After 600 ticks of a full battle (10 seconds, well past first contact), no two live
  soldiers are closer than `2 * kSoldierRadius` minus a tolerance.
- Rear-rank soldiers remain behind front-rank soldiers along the squad's facing.
- A squad at low morale has a strictly smaller mean inter-rank distance than the same
  squad at high morale.
- A squad in contact whose enemy is wiped out clears `contact` within `kContactClearTicks`
  and resumes advancing.

### 11.2 Archery

- An arrow does not hit a friendly placed at 20% of its path, and can hit one placed
  at 95%.
- Spread is strictly greater for a moving shooter than a stationary one at equal range.
- An archer that has just started moving does not shoot at full accuracy until
  `kSteadyTime` has elapsed.
- An infantry squad advancing on an archer squad flips it to `Withdraw` on the next tick,
  not on the next army-decide tick (section 8.5). The archers' centroid then moves away
  from the threat, and no arrows spawn while they withdraw.
- A squad whose only in-range target is surrounded by friendly squads holds fire.

### 11.3 Coordination

- Assignment is deterministic across repeated runs from the same state.
- Every archer squad with a threat inside `kScreenThreatRadius` receives at most one
  screen, and the screening squad's objective lies between its ward and the threat.
- No enemy squad is assigned more `Line` squads than its strength-proportional share plus
  one.
- A team with no archers assigns no `Screen` roles.

### 11.4 Regression

- `stateDigest` is identical at 1, 2, 8, and 15 worker threads, extended to cover the two
  new phases and all new digest-visible fields.
- Work counters remain thread-count invariant.
- Baselines in `tests/baseline/` regenerate; the README's published tick figures and state
  digest regenerate from a fresh benchmark run. Both are plan steps, not incidental edits.

---

## 12. Risks

- **A squad latched in contact against a wall, or onto an enemy that then dies.**
  `kContactClearTicks` and `Withdraw` overriding the halt are the mitigations, but this is
  the case most likely to need tuning against observed behavior. It is also the case whose
  failure mode is worst: a permanently frozen squad.
- **A routing squad jamming against a friendly line** under non-penetration. Asymmetric
  push weight (routing soldiers shove harder, or are shoved more easily) is the obvious
  fix and is deliberately **not** built now. It is speculative until the jam is observed.
- **Target spreading under-engaging a strong enemy squad.** Strength-proportional demand
  is the mitigation and section 11.3 is the test that catches it failing.
- **Scorer weights.** Two new terms join an already hand-tuned scorer. The terrain design
  carries the same caveat, and it compounds here. A tuning pass against observed behavior
  is a plan stage, not an afterthought.
- **Tick cost.** Section 4 states the expected increase. If `phaseContact` proves more
  expensive than one neighbor query per soldier suggests, the fallback is to run it every
  other tick with doubled displacement, which preserves determinism at the cost of
  slightly springier contacts.
- **Cohesion compression interacting with anchor latching.** Both move rear ranks forward.
  Their product could collapse a shaken engaged squad into a point. `kMinCompression`
  floors this, and the section 11.1 rank-order test is what detects it.

---

## 13. Non-Goals

- Player control or unit selection. This is a simulation, not a game.
- Pathfinding beyond the existing terrain candidate scorer. No navigation mesh, no A star.
- Per-soldier morale. Morale is a squad property; individual panic is out of scope.
- Formation shape changes at runtime (line into square against cavalry, and similar).
  Shape stays a function of unit type.
- Fatigue, ammunition limits, or weapon variety within a unit type.
- Asymmetric push weight in non-penetration (see section 12).

---

## 14. Success Criteria

1. Two infantry formations meeting produce a **line**, not a blob. Front ranks fight,
   rear ranks close up, ranks remain distinguishable, and neither squad's centroid passes
   through the other's.
2. A shaken squad is visibly less ordered than a fresh one, without any change to its
   formation shape.
3. Infantry squads are observably positioned between friendly archers and advancing
   threats, and the army does not converge on a single enemy squad.
4. Archers hold position to shoot, retreat when infantry closes, run faster while
   retreating, and do not fire behind themselves while running.
5. Arrows loosed over a friendly front rank do not harm it; arrows loosed into a mixed
   melee sometimes do.
6. `stateDigest` remains identical across worker-thread counts.
7. Tick cost at 10,000 agents is re-measured and republished, with the increase attributed
   to a named phase rather than left unexplained.
