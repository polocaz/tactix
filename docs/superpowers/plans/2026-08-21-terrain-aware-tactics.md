# Implementation Plan: Terrain-Aware Tactics

**Design:** `2026-08-21-terrain-aware-tactics-design.md`
**Date:** 2026-08-21
**Status:** In progress

This plan sequences the design into buildable, testable stages. Each stage is independently
verifiable and preserves thread-count determinism, which is the project's load-bearing
invariant (`stateDigest` identical across worker-thread counts).

## Stage 1 — Geometry foundation (TerrainField) *[correctness + shared helper]*

- New header `src/Terrain.hpp`: `Building`, `Tree`, `TerrainField`.
  - `insideAnyObstacle`/`clearOfObstacles`: move current `Simulation` logic verbatim (preserve
    behavior, not a retune).
  - `segmentBlocked(a,b)`: segment-vs-expanded-rect (buildings) and segment-vs-expanded-circle
    (trees), expanded by `kObstacleStandoff`. Deterministic, order-independent.
  - `clearanceAt(p)`: signed distance to nearest raw obstacle boundary (negative inside), min
    across obstacles. Scoring input only.
- `Simulation` owns a `TerrainField terrain`; `generateObstacles` populates it. Public
  `clearOfObstacles`/`insideAnyObstacle` become thin delegating wrappers (keeps public API and
  existing tests working). `phaseSoldierSteerChunk`/`phaseMovementChunk` read `terrain.*`.
- Tests (`test_terrain.cpp`): §11.2 coverage — inside detection, clear-of, blocked-expanded
  segment, tangent-outside-not-blocked, tree-expanded-blocked.

## Stage 2 — Safe deployment (correctness fix) *[§5]*

- `init`: clear the slot **after** jitter (§5.1). Jitter is kept as transient deployment noise;
  the persistent slot target is the cleared raw slot, so steering still settles correctly (§5.2).
- This intentionally moves the state digest (expected). Tests (`test_deployment.cpp`): §11.1 —
  every soldier after `init` is `!insideAnyObstacle`, for several seeds and scales.

## Stage 3 — Tactical anchors + squad scoring *[the feature]*

- SquadHot gains `objectiveX/Y` (where the centroid should aim) and `moveX/Y` (normalized
  movement direction for the advance lead), split from `facingX/Y` (enemy-facing, rendering).
- `selectTargetSquad` extended with candidate generation (§6) and a weighted scorer (§7.3):
  direct, flank-left/right, building corners, tree sides, archer standoff, fallback hold.
  Candidate count bounded 16–32. Deterministic candidate order with documented tie-break.
  Hysteresis on current objective (§12 risk: indecision). Direct-lane-clear path keeps direct
  advance (§7.4).
- Unit preference weights (§6.4): infantry clear-lane, archer standoff+side, cavalry wide flank.
- `slotWorldPosition` uses `moveX/Y` for `kAdvanceLead` instead of facing (§7.2). Facing still
  points at the enemy.
- Writes only the owning squad in phase 3 → determinism preserved.
- Outcome verified by `stateDigest` thread-invariance and new squad-decision tests (§11.3).

## Stage 4 — Debug overlays *[§10, optional]*

- Render tactical objective point, centroid→objective line, blocked-lane highlight, candidate
  anchors for one selected squad. Behavior must read clearly without them; overlays are aids.

## Build/verify order

1. Stage 1 + tests, build, run `tactix_tests`.
2. Stage 2 + tests, build, run `tactix_tests`; note new digest.
3. Stage 3, build, run thread-invariance + decision tests.
4. Stage 4 last.

## Risks carried forward

- Scorer weights need a tuning pass against observed behavior (design §12). First pass favors
  "terrain changes decisions only when it matters" (§7.4).
- Corner anchors crowding formations → score clearance against formation extent (§12).
