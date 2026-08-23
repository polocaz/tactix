# Tactix: Armor, Troop Classes, Formations and Shields

**Date:** 2026-08-22
**Status:** Draft design, pending implementation plan
**Scope:** Replaces the flat damage model with a staged penetration model, gives soldiers weapons,
armor and shields, turns `FormationShape` from geometry into doctrine, and adds an army-tier line
relief. Builds on
[`2026-08-22-engagement-and-unit-ai-design.md`](2026-08-22-engagement-and-unit-ai-design.md) and
preserves the performance foundation's determinism constraints unchanged.

Deliberately independent of
[`2026-08-22-heightfield-terrain-design.md`](2026-08-22-heightfield-terrain-design.md). Shield
resolution keys off the horizontal impact direction only, so the heightfield's real ballistics can
replace the arc approximation underneath it without changing a rule in this document. Neither
design blocks the other, and either may be implemented first.

---

## 1. Goal

Every soldier in the simulation is currently interchangeable. Two of them differ in speed, range
and a health pool of two or three points, and in nothing else. A blow always does exactly one
damage, an arrow always has the same 45 percent chance to wound whoever it crosses, and a formation
is a pattern of slot offsets with no consequence at all once the fighting starts.

The battle therefore has no material texture. There is no reason to prefer a spear to a sword, no
reason a line would ever close up, and nothing an army can do about incoming arrows except take
them.

The deliverables are:

1. **A penetration model.** Geometry decides whether a man is struck, a shield may block, armor may
   turn the point. Three stages, each one integer roll.
2. **Troop classes.** Named presets combining a weapon, armor and a shield, layered over the
   existing `UnitType` rather than replacing it.
3. **Formations as doctrine.** Shieldwall, phalanx, testudo, manipular and mob, each carrying
   speed, turn rate, fighting depth, reach and per-arc shield cover, chosen dynamically by the
   squad and paid for with a transition window.
4. **Shield logic.** A shield covers the arc a man faces, so a volley into a flank is worth far
   more than the same volley into a face.
5. **Legion line relief.** A bloodied front-line maniple retires through the interval behind it and
   a fresh one steps up.
6. **A readable render.** Formation and protection legible at squad scale zoomed out and at man
   scale zoomed in, without adding an unconditional per-soldier draw call.

---

## 2. Current State

Findings are from reading the tree on 2026-08-22 at branch `docs-viewer`.

### 2.1 Damage is a constant

`applyMeleeIntents` ([`c++/src/Combat.cpp`](../../../c++/src/Combat.cpp)) subtracts `kMeleeDamage`
from the target and sets a cooldown. There is no roll: a swing that reaches always wounds.
`applyProjectileHits` ([`c++/src/Projectiles.cpp`](../../../c++/src/Projectiles.cpp)) rolls once
against `kArrowHitChancePct`, a single global constant that makes no reference to who was hit.

Both sites already have exactly the structure this design needs. They are serial, they run once per
blow, and they already draw from `Rng`. No part of the pipeline has to move.

### 2.2 `FormationShape` is geometry and nothing else

`Line`, `Column`, `Wedge` and `Loose` differ only in the slot offsets `formationSlot` returns.
`shapeForUnit` picks one per unit type at deployment and it never changes again. Nothing reads the
shape at resolution time.

`formationSlot` and `rankOfSlot` carry a comment warning that they must mirror each other by hand,
because each hardcodes the same aspect and spacing values in its own switch. That is a standing
hazard today. Adding five shapes to it would make it a serious one.

### 2.3 Only the front rank ever fights, and only implicitly

`selectMeleeTarget` queries `kMeleeReach` for every soldier. The front rank is the rank that fights
purely because it is the rank standing close enough. There is no notion of a weapon that reaches
past the man in front of you.

### 2.4 The RNG makes extra rolls free

`Rng` ([`c++/src/Rng.hpp`](../../../c++/src/Rng.hpp)) is a stateless PCG hash of seed, agent index,
tick and a `RngUse` enumerator. Draws have no sequence and no shared state, so adding rolls cannot
reorder or perturb any existing draw. Every new draw in this design appends one enumerator
immediately before `Count`, per the rule that file already states.

### 2.5 The renderer draws every soldier, on screen or not

`drawSimulation` ([`c++/src/Renderer.cpp`](../../../c++/src/Renderer.cpp)) loops the full soldier
array with no viewport test. At the zoom levels where this design wants to draw shields, most of
the array is off screen, and raylib still batches a quad for each one. Section 8 adds the cull,
because a per-soldier shield draw is only affordable behind it.

---

## 3. Data Model

### 3.1 Three orthogonal enums and one preset

All appended-only, for the reason `SquadOrder` already documents: enumerator values feed the state
digest, so renumbering one silently invalidates every committed baseline.

```cpp
enum class WeaponClass : uint8_t { Sword=0, Spear=1, Bow=2, Javelin=3, Lance=4 };
enum class ArmorClass  : uint8_t { None=0, Padded=1, Mail=2, Plate=3 };
enum class ShieldClass : uint8_t { None=0, Buckler=1, Round=2, Tower=3 };
enum class TroopClass  : uint8_t { Levy=0, Legionary=1, Hoplite=2, Huscarl=3,
                                   Archer=4, Skirmisher=5, Knight=6 };

constexpr uint32_t kWeaponCount = 5, kArmorCount = 4, kShieldCount = 4, kTroopCount = 7;
```

`UnitType` is untouched. It remains the coarse role that the army tier, `shapeForUnit`,
`kStrengthPerMan` and the screening logic key off. `TroopClass` is the fine grain, and one table
connects the two:

| Troop | UnitType | Weapon | Sidearm | Armor | Shield | Discipline |
| --- | --- | --- | --- | --- | --- | --- |
| Levy | Infantry | Spear | Spear | Padded | Round | 0.55 |
| Legionary | Infantry | Javelin | Sword | Mail | Tower | 0.90 |
| Hoplite | Infantry | Spear | Spear | Mail | Round | 0.85 |
| Huscarl | Infantry | Sword | Sword | Mail | Round | 0.88 |
| Archer | Archer | Bow | Sword | Padded | None | 0.60 |
| Skirmisher | Archer | Javelin | Sword | None | Buckler | 0.50 |
| Knight | Cavalry | Lance | Sword | Plate | Round | 0.70 |

`discipline` moves out of the `kDisciplineInfantry` / `kDisciplineArcher` / `kDisciplineCavalry`
constants and into this table, and those three constants are deleted. Discipline was always a
property of who the men are rather than of what role they fill, and this is the first version of
the data model able to say so.

The sidearm column serves exactly one behavior. A legionary throws his pilum once inside
`kPilumRange` and then fights with a sword. Every other troop's sidearm equals its weapon, so the
switch is a table read rather than a branch.

### 3.2 Storage

One field is added to `SoldierHot`:

```cpp
std::vector<uint8_t> troopClass;   // TroopClass
```

That is 10 KB at 10,000 agents. Weapon, armor and shield are reached through `kTroopLoadout` rather
than stored per soldier, because they are read at **resolution** time, which is once per blow (low
hundreds per tick) and not once per soldier per tick (10,000). Trading two L1 table reads for two
bytes per soldier is the right side of that trade.

`SoldierHot::spawn` takes the troop class and derives `unitType` and `maxHealth` from it, so a
caller cannot construct a soldier whose role and loadout disagree. `SoldierHot::clear` gains the
field, per the warning that struct already carries about its two field lists drifting apart.

`maxHealth` moves from `kUnitStats` into the troop table for the same reason discipline did. Speed
and range stay on `kUnitStats`, because they are genuinely properties of the role: a mounted man is
fast because he is mounted.

### 3.3 The two combat tables

```cpp
// Chance in percent that a landed blow wounds. Geometry decides whether a man
// is STRUCK; this decides whether it goes through what he is wearing.
constexpr uint8_t kWoundChancePct[kWeaponCount][kArmorCount] = {
    /*             None  Padded  Mail  Plate */
    /* Sword   */ {  85,     70,   40,    20 },
    /* Spear   */ {  80,     65,   45,    30 },
    /* Bow     */ {  75,     55,   30,    12 },
    /* Javelin */ {  85,     75,   60,    40 },
    /* Lance   */ {  95,     90,   75,    55 },
};

// Chance in percent that a shield stops a missile arriving in the FRONT arc.
constexpr uint8_t kShieldCoverPct[kShieldCount] = { 0, 20, 40, 55 };

// Front, side, rear. A shield covers what a man faces and nothing behind him.
constexpr float kArcCoverScale[3] = { 1.0f, 0.35f, 0.0f };

// A shield stops an arrow better than it stops a man determined to push past it.
constexpr float kShieldMeleeScale = 0.6f;

// Ceiling on any block chance. Nothing is ever certain, and without this a
// tower shield in testudo reaches 110 and is literally invulnerable.
constexpr uint8_t kMaxBlockPct = 90;
```

Three entries carry most of the design's intent and are worth stating rather than leaving to be
inferred from the numbers:

- **Javelin against mail (60)** is the pilum. Deliberately mediocre against bare flesh and the best
  thing in the game against armor, which is what makes the legion's volley a decision rather than a
  free opener.
- **Bow against plate (12)** keeps archery a screening and morale weapon instead of a knight-killer.
  Arrows should break formations, not break knights.
- **Spear against plate (30) beating sword against plate (20)** is the point concentrating force.
  It is also the reason a spear is worth carrying once the phalanx bonus is set aside.

### 3.4 Armor costs movement

```cpp
constexpr float kArmorSpeedScale[kArmorCount] = { 1.00f, 0.97f, 0.92f, 0.86f };
```

Applied through the `speedScale` parameter `steerToward` already accepts, multiplied with the
formation's speed scale and the existing flight multiplier. Plate has to cost something or it is
not a choice.

---

## 4. Formations as Doctrine

### 4.1 The shapes

`FormationShape` gains five entries, appended: `Shieldwall=4, Phalanx=5, Testudo=6, Manipular=7,
Mob=8`.

A single `kFormationTraits[shape]` table drives both geometry and behavior:

| Shape | spacing | aspect | speed | turn | fighting ranks | reach | cooldown | cover F / S / R |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Line | 1.00 | 2.0 | 1.00 | 1.00 | 1 | 1.0 | 1.0 | 0 / 0 / 0 |
| Column | 1.00 | 0.5 | 1.00 | 1.00 | 1 | 1.0 | 1.0 | 0 / 0 / 0 |
| Wedge | 1.00 | n/a | 1.00 | 1.20 | 1 | 1.0 | 1.0 | 0 / 0 / 0 |
| Loose | 2.00 | 2.0 | 1.00 | 1.00 | 1 | 1.0 | 1.0 | 0 / 0 / 0 |
| Shieldwall | 0.75 | 3.0 | 0.60 | 0.50 | 1 | 1.0 | 1.0 | +30 / +5 / 0 |
| Phalanx | 0.85 | 1.5 | 0.50 | 0.35 | 3 | 1.6 | 1.0 | +20 / 0 / 0 |
| Testudo | 0.60 | 1.2 | 0.30 | 0.40 | 1 | 1.0 | 2.2 | +55 / +45 / +35 |
| Manipular | 1.00 | 2.5 | 0.95 | 1.00 | 1 | 1.0 | 1.0 | +10 / 0 / 0 |
| Mob | 1.50 | 1.0 | 1.05 | 2.00 | 1 | 1.0 | 1.2 | -10 / -10 / -10 |

The first four rows reproduce today's behavior exactly, so introducing the table is not a behavior
change for existing shapes.

Four columns are load-bearing rather than flavor:

- **`turn`** multiplies the existing `kFacingSlewRate`. A phalanx at 0.35 needs roughly four seconds
  to face a threat it started perpendicular to, so cavalry that gets around its flank stays there.
  The formation's historic weakness falls out of a mechanism already in the codebase instead of
  being a special case bolted on.
- **`fightingRanks`** replaces the implicit "whoever is close enough" rule of section 2.3. A
  phalanx resolves melee from ranks 0, 1 and 2 against enemies in the front arc, at `reach` times
  `kMeleeReach`. That is what lets a spear wall grind down a shieldwall frontally and lose to it
  once flanked.
- **`cooldown`** multiplies `kMeleeCooldown`. Testudo at 2.2 is the price of the cover: men under
  their shields fight badly, and without this testudo is a free win in melee as well as against
  arrows.
- **`cover`** is added to the shield block chance from section 3.3, per arc, in percentage points.
  Testudo's numbers are additive with a tower shield's 55, which puts frontal missile protection at
  the `kMaxBlockPct` ceiling and leaves the rear arc near 35. A testudo is not immune. It is hard
  from the front and merely good from behind.

### 4.2 Geometry becomes data-driven

`formationSlot` and `rankOfSlot` read `spacing` and `aspect` from `kFormationTraits` instead of
each carrying its own switch. The mirror those two functions currently maintain by hand and by
comment becomes structural: there is one definition of a shape's width, and both functions call it.

`Wedge` remains the one special case, since its rank layout is `floor(sqrt(i))` rather than a grid
division, and both functions keep their existing early return for it.

`Manipular` is `Line` with an interval inserted every `kManipleWidth` (8) columns, shifting
subsequent columns outward by `kManipleInterval` (one slot spacing). The gaps are not decoration:
they are the corridor the relief in section 6 retires through.

`Mob` scatters its slots by hashing the slot index through `pcgHash` into a bounded offset. It is
deterministic (a pure function of slot index and squad id, drawing no `Rng` state), it never
overlaps because the offset is bounded under `kSeparationRadius`, and it reads as a crowd rather
than a grid.

### 4.3 Choosing a formation

Per squad, evaluated in `squadDecide` (phase 4), in priority order. First match wins:

1. Order is `Rout`, or troop discipline is below `kMobDisciplineFloor` (0.45) while in contact, gives
   **Mob**.
2. `missilePressure` above `kTestudoThreshold`, squad not in contact, shield class is `Tower`, gives
   **Testudo**.
3. In contact, or `nearestEnemyDist` below `kImminentContactDist`, gives the best melee shape the
   loadout supports: **Phalanx** if the weapon is a spear, **Shieldwall** if the shield is `Round`
   or better, otherwise **Line**.
4. Troop is `Legionary` gives **Manipular**; otherwise the existing `shapeForUnit` result.

Eligibility is a property of the loadout, not of the situation, so a squad can never adopt a
formation its equipment does not support. That check lives in one predicate,
`formationAvailable(troop, shape)`, used both here and by the tests.

### 4.4 `missilePressure` costs nothing

Detecting incoming fire needs no new pass. Resolution already walks every arrow that struck a
soldier. When a strike resolves, it increments `missilePressure` on that soldier's squad;
`squadDecide` decays it by `kMissilePressureDecay` per second.

Two consequences, both wanted. The testudo forms in response to arrows that actually arrived rather
than to arrows that might, which is legible on screen. And it lags the first volley by a tick,
which is correct: a real unit also learns it is under fire by being under fire.

### 4.5 Transitions have a cost

Three fields per squad: `prevShape`, `shapeBlend` (seconds remaining, counting down) and
`formationHold` (seconds before another change is allowed).

While `shapeBlend > 0`, `slotWorldPosition` linearly interpolates between `formationSlot(prevShape)`
and `formationSlot(shape)` over `kFormationChangeSeconds` (1.4s). `formationSlot` is therefore
called twice per soldier only while that soldier's squad is actually changing shape, and once
otherwise.

During the blend the squad takes the **minimum** cover of the two shapes per arc and the **slower**
of the two speed scales. That is the whole cost, and it is a real one: a squad caught mid-drill is
worse off than one that stood still and took the volley.

`formationHold` is set to `kFormationHoldSeconds` (3.0) on every change. It is the hysteresis that
prevents oscillation, in the same spirit as `kContactClearSeconds` and the rally threshold gap.

---

## 5. Resolution: Shields and Armor

### 5.1 One function, both weapons

```cpp
// Percent chance a shield stops a blow arriving along (impactDirX, impactDirY),
// which is the direction the blow TRAVELS. Melee blows scale by
// kShieldMeleeScale. Reads the target's squad facing, its shield class and its
// formation traits; writes nothing.
uint8_t shieldBlockPct(const SoldierHot& soldiers, const SquadHot& squads,
                       uint32_t target, float impactDirX, float impactDirY,
                       bool melee);
```

Arc classification uses the target's **squad facing**, not its per-soldier `dirX/dirY`. A soldier
standing still in a fight retains whatever direction he was last moving in, which is stale and
sometimes meaningless; the squad facing is the deliberate statement of where the formation points,
and it is what the shields of a formed body actually follow. The cost is one indirect load through
`squadId` per resolved blow.

```
cos  = -(impactDir . squadFacing)      // 1.0 means straight into the face
arc  = cos >  0.5 ? Front
     : cos > -0.5 ? Side
     :              Rear
pct  = kShieldCoverPct[shield] * kArcCoverScale[arc] + traits.cover[arc]
pct  = melee ? pct * kShieldMeleeScale : pct
return clamp(pct, 0, kMaxBlockPct)
```

The arc thresholds at plus and minus 0.5 give a 120 degree frontal arc, a 120 degree band of side,
and a 120 degree rear. Even thirds are the honest default and nothing in the design argues for
skewing them.

### 5.2 Missiles

`applyProjectileHits` becomes three stages. `ProjectileHot` gains a `weapon` field (uint8), because
the shooter's index is not reliable by the time an arrow lands: he may be dead and compaction
renumbers survivors. The arrow carries what threw it.

```
1. Geometry (unchanged): the swept segment test already wrote intentHitTarget.
2. Block:  rng.range(projectileIndex, RngUse::ShieldBlockRoll, 1, 100)
             <= shieldBlockPct(..., melee=false)
           -> arrow spent, no wound.
3. Wound:  rng.range(projectileIndex, RngUse::MissileWoundRoll, 1, 100)
             <= kWoundChancePct[arrow.weapon][target.armor]
           -> health -= damage.
```

**`kArrowHitChancePct` is deleted.** This is the single most consequential line in the design, and
it is a replacement rather than an addition. Multiplying a flat 45 percent by a block roll and a
wound roll would put a bowman's chance against a shielded, mailed man near 8 percent and make
archery ornamental. The two new rolls *are* the old constant, decomposed into terms that name what
they represent.

`RngUse::ArrowHitRoll` is **not** removed from the enum, per the rule in `Rng.hpp` that deleting an
enumerator reshuffles every value after it. It stays, unused, with a comment saying so.

### 5.3 Melee

`applyMeleeIntents` gains the same two stages, with the impact direction being the normalized
attacker-to-target vector.

Two rules that are easy to get wrong and are load-bearing:

- **The cooldown is set whether or not the blow lands.** Today the cooldown is only set on a
  successful application. If a blocked blow left the cooldown clear, an attacker would re-roll every
  tick until he got through, and shields would reduce to a small delay rather than a defense. A
  parried swing costs you the swing.
- **The wound roll uses the attacker's current weapon**, which for a legionary inside melee range is
  his sidearm, not the pilum he has already thrown.

Cooldown is scaled by the attacker's formation `cooldown` trait, which is how testudo pays for its
cover.

### 5.4 Fighting depth and reach

`selectMeleeTarget` gains two changes:

- Query radius becomes `kMeleeReach * traits.reach` for soldiers whose rank is under
  `traits.fightingRanks`, and stays `kMeleeReach` for everyone else. Rear ranks keep the query they
  have today, so a squad that is flanked or has enemies inside it can still defend itself. This is
  deliberately *not* the more aggressive optimization of skipping the query for rear ranks entirely:
  that would save roughly 78 percent of melee queries and would also make a squad attacked from
  behind unable to fight back at all. Correctness first; the optimization is noted in section 10 as
  a measurable follow-up with that risk named.
- A soldier in rank 1 or deeper may only select a target in his squad's **front** arc, tested with
  the same 0.5 cosine as section 5.1. Spears reach past the man in front of you, not around him.

The added cost is confined to the front ranks of spear squads, which query 1.6 times the radius and
therefore about 2.56 times the area. Section 10 budgets it.

### 5.5 The pilum volley

A legionary squad whose `nearestEnemyDist` first falls below `kPilumRange` (90px) and whose
`pilumSpent` flag is clear sets `intentFire` on every member for one tick, spawns javelins through
the existing `spawnArrows` path with `WeaponClass::Javelin` and `kJavelinSpeed`, and sets
`pilumSpent`. The flag clears on rally, never mid-battle.

This reuses the entire projectile pipeline. The only new code is the trigger condition and one
`uint8_t` per squad.

---

## 6. Legion Line Relief

### 6.1 What it is

The distinctive legion behavior is not a formation, it is rotation. A front-line maniple that has
been fighting long enough to be spent retires through the interval behind it and a fresh maniple
steps into its place.

Every mechanism this needs already exists: `SquadRole::Reserve`, `SquadOrder::Withdraw` (documented
as keeping formation and rallying on command, precisely unlike `Rout`), `kReserveDepth`, and
`phaseArmyDecide` running serially every `kArmyDecideInterval` (30) ticks over two armies.

### 6.2 The rule

Four new per-squad fields: `initialMemberCount` (set at deployment), `contactDuration` (counts up
while `contact` is set, resets when it clears), `reliefCooldown` (counts down, blocks re-entry) and
`reliefStage` (0 for not relieving, 1 and 2 for the stages in 6.3).

In `phaseArmyDecide`, for each army, a front-line squad is **spent** when its shape is `Manipular`,
its `contact` flag is set, its `reliefCooldown` is clear, and any of:

- `memberCount < initialMemberCount * kReliefLossFraction` (0.60)
- `morale < kReliefMoraleThreshold` (0.55)
- `contactDuration > kReliefContactSeconds` (20.0)

For each spent squad, in ascending squad index (so the choice does not depend on iteration order),
find the nearest same-team `Reserve` squad within `kReliefSearchRadius` that is not already
relieving. If one exists, pair them and set `reliefCooldown` on both.

### 6.3 The passage, in two stages

Two squads swapping places is the part most likely to look wrong, because the existing
non-penetration pass will happily jam them into each other. The relief is therefore staged, with a
`reliefStage` field on both squads:

- **Stage 1.** The spent squad is ordered `Withdraw` toward a point `kReliefRetireDistance` directly
  behind its own anchor. The reserve simultaneously advances to the **interval** position: the front
  squad's anchor offset laterally by half a maniple width, which is exactly the gap `Manipular`
  leaves.
- **Stage 2.** Once the two centroids are more than `kReliefClearDistance` apart, the reserve
  shifts laterally onto the front anchor and takes `SquadRole::Line`. The retired squad takes
  `SquadRole::Reserve` and holds at `kReserveDepth`.

The lateral offset is what makes this work without any new collision logic: the two squads are never
aiming at the same point at the same time.

**Acceptance criterion, stated up front because this is the risky part:** a relief completes with
the two squads' member sets never interpenetrating, and the front line does not open a gap wider
than one maniple at any point during it. If staging cannot achieve that, the fallback is to relieve
only when the spent squad's contact has cleared, which is less impressive and always safe.

---

## 7. Determinism and Phase Safety

Nothing in this design adds a parallel phase or a cross-agent write outside serial resolution.

- `shieldBlockPct` reads soldiers, squads and constant tables, and writes nothing. It is called only
  from serial resolution.
- `missilePressure` is written only in serial resolution and read only in phase 4.
- Formation choice is written in phase 4 by the squad that owns it, reading other squads' centroids,
  which phase 2's barrier has already made read-only. This is the same rule `squadDecide` follows
  today.
- Relief is decided in `phaseArmyDecide`, which is serial over two entities.
- Every new random draw appends a `RngUse` enumerator before `Count`: `ShieldBlockRoll`,
  `MissileWoundRoll`, `MeleeBlockRoll`, `MeleeWoundRoll`. No existing enumerator is reordered or
  deleted. `Mob` slot jitter adds none of its own: per section 4.2 it is a pure `pcgHash` of slot
  index and squad id, reproducible without touching the `Rng` at all.

**The state digest baseline changes.** `troopClass` enters `SoldierHot`, new squad fields enter
`SquadHot`, and the damage model changes outcomes from the first tick a blow lands. The README's
published digest `1c7f65a50c1c1f5c` for seed 42 must be regenerated and republished as part of this
work, not silently.

---

## 8. Rendering

Three tiers, gated on the zoom thresholds `drawSimulation` already computes.

### 8.1 A viewport cull comes first

Section 2.5: the soldier loop currently draws every agent regardless of where the camera is. Before
any per-soldier detail is added, the loop gains a bounds test against the camera rectangle expanded
by a small margin. This is a prerequisite rather than an optimization: without it, the tier 2 and 3
draws below are paid for 10,000 soldiers no matter how few are visible.

### 8.2 Tier 1, zoomed out (`zoom < 1.1`): formation reads at squad scale

The squad overlay already draws a translucent disc, a morale ring, a contact ring and a facing tick.
It gains a formation glyph drawn inside the disc, at a cost of two to six primitives per squad
across roughly 200 squads:

| Shape | Glyph |
| --- | --- |
| Testudo | Filled square at high alpha, no facing tick. A closed box. |
| Shieldwall | Filled disc with a thick arc across the front third. |
| Phalanx | Filled disc with three short hedge lines projecting forward. |
| Manipular | Filled disc split by a vertical gap, echoing the interval. |
| Mob | No hull. Scattered dots at member positions, at low alpha. |

Mean armor of the squad shifts the disc's value the way unit type already shifts a soldier's: an
armored squad reads heavier without either team drifting toward the other's hue.

### 8.3 Tier 2, mid zoom (`1.1 <= zoom < 2.2`): shields per man

One extra `DrawRectangleV` per **visible** soldier: a 2px bar offset along the squad facing, on the
soldier's front side, colored by shield class (none draws nothing, so unshielded troops cost zero).
Armor rides on the existing body color as brightness, exactly as health does today, and costs no
additional draw at all.

At this zoom the visible soldier count is bounded by the viewport, which after 8.1 is what is
actually iterated.

### 8.4 Tier 3, close (`zoom >= 2.2`): weapons

A line along the facing for the weapon, long for a spear (reaching past the shield, which is the
whole point of a spear and should be visible), short for a sword, and the existing health pips.
Bows and lances get their own lengths. At this zoom a few dozen men fill the screen.

---

## 9. Testing

New files mirroring the existing doctest layout, plus additions to current ones.

**`tests/test_armor.cpp`**
- `kWoundChancePct` is monotonically non-increasing across armor classes for every weapon row. A
  property over the table, so a future tuning edit that inverts a row fails immediately.
- A plate-armored target under a fixed volley at a fixed seed survives strictly longer than an
  unarmored one.
- Armor speed scale is applied: a plate squad's centroid advances less far in 200 ticks than a
  padded one's.

**`tests/test_shields.cpp`**
- An identical volley into a squad's rear kills strictly more men than into its front.
- A `ShieldClass::None` squad blocks nothing: block chance is exactly 0 in all three arcs.
- A testudo under sustained fire takes under 40 percent of the casualties the same squad takes in
  `Line`, same seed, same volley.
- `kMaxBlockPct` is never exceeded by any shield and formation combination, checked over the full
  cross product of both tables.
- A blocked melee blow still consumes the attacker's cooldown.

**`tests/test_formation.cpp`** (additions)
- `rankOfSlot` agrees with the rank implied by `formationSlot` for every new shape across member
  counts 1 to 200. This is the property the hand-mirrored switches could not guarantee.
- Every shape's `formationMeanOffset` still makes the centroid a fixed point, including `Mob`.
- Mid-transition cover equals the per-arc minimum of the two shapes.
- At exactly `kFormationChangeSeconds`, a blending slot equals the target shape's slot bit for bit.

**`tests/test_squads.cpp`** (additions)
- A squad cannot change shape twice inside `kFormationHoldSeconds`.
- Testudo is never adopted by a squad without tower shields, under any missile pressure.
- `missilePressure` decays to zero within the expected time after fire stops.

**`tests/test_combat.cpp`** (additions)
- A phalanx kills from rank 2, and does not kill from rank 2 against a target outside the front arc.
- A phalanx engaged frontally loses fewer men per second than the same phalanx engaged from the
  flank, same seed.

**`tests/test_army.cpp`** (additions)
- A spent front-line maniple is relieved within 90 ticks of meeting the criteria.
- The relieving squad's final centroid is closer to the enemy than the relieved squad's.
- Relief does not fire twice inside `reliefCooldown`.
- The member sets of the two squads never interpenetrate during the passage (section 6.3's
  acceptance criterion, as an assertion rather than an aspiration).

**`tests/test_determinism.cpp`**
- Unchanged assertions against a regenerated digest baseline, and the existing 1-worker versus
  15-worker equality.

---

## 10. Performance Budget

Stated as predictions to be measured, in the attribution style the README already uses.

**Memory.** Plus one byte per soldier (10 KB at 10,000). Plus roughly 24 bytes per squad across
about 200 squads, which is noise.

**Resolution.** Plus one dot product, one indirect load and two integer rolls per resolved blow.
Blows per tick are bounded by melee contacts plus arrow strikes, in the low hundreds. This should be
unmeasurable.

**Steering.** Plus one `formationSlot` call per soldier, only while that soldier's squad is inside
its 1.4 second transition window. Bounded by `kFormationHoldSeconds`: a squad spends at most 1.4 of
every 4.4 seconds transitioning, worst case.

**Melee queries.** Front ranks of spear squads query 1.6 times the radius, so about 2.56 times the
candidate area. The front rank is roughly a fifth of a squad, so the expected added melee-query cost
is around 0.3 times the current melee-query cost, for spear squads only. `phaseContact` remains the
dominant neighbor-query consumer at about 3.1 ms of the 9.09 ms p50.

**Army tier.** The relief scan is O(squads) inside a phase that already runs every 30 ticks over two
entities.

**Rendering.** The viewport cull in 8.1 should be a net *reduction* at every zoom above 1.0, which
is where the new per-soldier draws live.

**The real cost is pacing, not mechanism.** Armored men live longer, so a fixed-length benchmark run
simulates a fuller field for more of its ticks. This is the same effect the README already documents
for `kArrowHitChancePct`, where a gameplay change doubled p50 while cost per live agent fell. The
measurement plan follows that precedent exactly:

1. Measure p50/p95/p99/max at 10,000 agents, seed 42, 2000 ticks.
2. Re-measure with every `kWoundChancePct` entry forced to 100, which restores the old lethality
   while keeping every new code path live.

The difference between those two runs is the pacing change; the difference between run 2 and today's
9.0937 ms p50 is the mechanism. Both numbers go in the README, with the digest baseline regenerated.

**Follow-up, not in scope here:** skipping the melee neighbor query entirely for ranks at or beyond
`fightingRanks` would remove roughly 78 percent of melee queries. It is not in this design because
it makes a squad attacked from behind unable to fight back. Doing it correctly needs a cheap
per-squad "enemy inside our ranks" flag first, which is its own piece of work.

---

## 11. Risks

**Testudo dominance.** If `kTestudoThreshold` is too low, every shielded squad turtles under the
first volley and the battle stalls into two immobile boxes. Mitigations are all already in the
design: speed 0.30, cooldown 2.2, no adoption while in contact, and a 1.4 second window during which
the squad has the worse of both shapes. Acceptance check: a testudo caught by cavalry is destroyed
faster than a shieldwall would be.

**Phalanx dominance.** Three fighting ranks at 1.6 reach is a large frontal advantage. This is
intended: a frontal phalanx *should* win. The counter is the 0.35 turn rate, and the acceptance
check is that cavalry reaching a phalanx's flank beats it reliably. If it does not, the turn rate
drops before the fighting ranks do.

**Relief passage jamming.** Named with its own acceptance criterion and its own fallback in section
6.3.

**Battles becoming too long.** Three rolls where there was one means fewer casualties per tick.
Tuning order if a 2000-tick run stops resolving: raise the `kWoundChancePct` rows, then lower shield
cover, then reduce `maxHealth`. Formation traits are tuned last, because they are what the design
exists to demonstrate.

**Table sprawl.** Five tables now govern combat outcomes. The mitigation is that they are small,
`constexpr`, and each has a property test in section 9 that fails on a nonsensical edit rather than
producing a quietly worse battle.

---

## 12. Deferred

Explicitly out of scope, recorded so the omissions read as decisions:

- **Overhead cover as a fourth arc.** Plunging fire arriving over a shield rim needs the vertical
  axis the heightfield design introduces. Testudo's all-around cover approximates it for now.
- **Fatigue.** Nothing tires. `contactDuration` exists in section 6 and would be the natural input,
  but nothing has asked for it.
- **Mixed-loadout squads.** A squad is uniform. A legion with shielded front ranks and unshielded
  rear ranks is more accurate and would touch deployment, formation and rendering together.
- **Player-issued formation orders.** Formation is chosen by the squad AI. A commanding UI is a
  different project.
- **Weapon reach affecting non-melee.** `reach` scales melee only.

---

## Appendix A: New Constants

Every constant this design names, with the value the implementation starts from. All are tuning
knobs; the existence of each is not. Values already stated in the body are repeated here so there is
one place to look.

| Constant | Value | What it decides |
| --- | --- | --- |
| `kMaxBlockPct` | 90 | Ceiling on any shield block chance |
| `kShieldMeleeScale` | 0.60 | How much less a shield helps against a man than against an arrow |
| `kFormationChangeSeconds` | 1.4 s | Transition window, during which cover is the worse of both shapes |
| `kFormationHoldSeconds` | 3.0 s | Minimum time in a shape before another change is allowed |
| `kMobDisciplineFloor` | 0.45 | Discipline below which a squad in contact degenerates to a mob |
| `kMobJitter` | 4.0 px | Bound on Mob slot scatter. MUST stay under `kSeparationRadius` (10) |
| `kImminentContactDist` | 60 px | Enemy distance at which a squad adopts its melee shape |
| `kMissilePressurePerHit` | 0.30 | Added to a squad when one of its men is struck |
| `kMissilePressureDecay` | 0.60 /s | How fast that pressure bleeds off |
| `kTestudoThreshold` | 1.00 | Pressure at which a tower-shield squad closes up. With the two rows above, roughly two hits a second sustained |
| `kManipleWidth` | 8 | Columns per maniple before an interval is inserted |
| `kManipleInterval` | 12 px | Interval width, one `kSlotSpacing`. This is the corridor a relief retires through |
| `kPilumRange` | 90 px | Distance at which a legionary squad throws |
| `kJavelinSpeed` | 260 px/s | Faster and flatter than an arrow's 200 |
| `kJavelinLifetime` | 0.6 s | Enough for `kPilumRange` with margin, well under the arrow's 3.0 |
| `kReliefLossFraction` | 0.60 | Survivor fraction below which a maniple counts as spent |
| `kReliefMoraleThreshold` | 0.55 | Morale below which it counts as spent |
| `kReliefContactSeconds` | 20.0 s | Time in contact after which it counts as spent regardless |
| `kReliefSearchRadius` | 260 px | How far back a relieving reserve may be found |
| `kReliefRetireDistance` | 140 px | How far the spent maniple withdraws |
| `kReliefClearDistance` | 70 px | Centroid separation at which stage 2 begins |

Three of these have ordering constraints against constants that already exist, and all three are
stated here rather than left to be discovered:

- `kMobJitter` (4.0) MUST stay under `kSeparationRadius` (10), or a mob's own slots push its members
  apart and the formation fights the separation force forever. This is the same failure
  `kSeparationRadius` documents against `kSlotSpacing`.
- `kReliefMoraleThreshold` (0.55) MUST stay above `kRallyThreshold` (0.45), or a maniple routs
  before it is ever judged spent and the relief tier never fires at all.
- `kReliefRetireDistance` (140) MUST stay above `kReserveDepth` (120), or the retiring maniple stops
  inside the reserve line it is trying to fall behind.
