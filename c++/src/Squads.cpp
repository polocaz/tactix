#include "Squads.hpp"
#include "Simulation.hpp"
#include "Formation.hpp"
#include "DetMath.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Scoring weights for chooseTacticalObjective (design §7.3). All are tuning
// knobs; the EXISTENCE of each term is not. See the design for what each
// term loads and unloads.
constexpr float kFlankDistance       = 70.0f;   // side offset of flank anchors
constexpr float kBlockedLanePenalty  = 1.0e6f;  // dominates any anchor whose legs cross terrain
constexpr float kDistanceWeight      = 1.0f;    // prefer closer objectives
constexpr float kClearanceWeight     = 6.0f;    // open ground is good
constexpr float kClearanceCap        = 40.0f;   // saturation point of clearanceBonus
constexpr float kFormationRoomWeight = 6.0f;    // room for the formation footprint
constexpr float kTargetPressureWeight= 3.0f;    // keep moving toward the enemy
constexpr float kHysteresisWeight    = 25.0f;   // damp anchor flip-flop (design §12)
constexpr float kArcherStandoffWeight= 20.0f;   // archers want to shoot, not march
constexpr float kCavalryTightPenalty = 40.0f;   // cavalry hates tight terrain

float squadHalfExtent(const SquadHot& squads, size_t s) {
    const FormationShape shape = shapeForUnit(squads.unitType[s]);
    const Vec2 ext = formationExtent(shape, squads.memberCount[s]);
    return 0.5f * std::max(ext.x, ext.y);
}

Vec2 normalizeSafe(Vec2 v, Vec2 fallback) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-6f) return fallback;
    return { v.x / len, v.y / len };
}

// Closest point on segment a->b to point p (used for tree-anchor placement).
Vec2 closestOnSegment(Vec2 a, Vec2 b, Vec2 p) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    float t = 0.0f;
    if (lenSq > 1e-12f) {
        t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    }
    return { a.x + dx * t, a.y + dy * t };
}
} // namespace

void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members,
                         std::vector<uint32_t>& countsScratch,
                         std::vector<uint32_t>& cursorScratch) {
    const size_t squadCount = squads.count;

    // Counting pass. Dead soldiers are excluded so that a squad's range holds
    // only live members; plan 2's compaction removes them from the array.
    // countsScratch/cursorScratch are caller-owned so this serial, once-a-
    // tick call does not heap-allocate two vectors every tick -- assign()
    // reuses existing capacity instead of freeing and reallocating.
    countsScratch.assign(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        countsScratch[soldiers.squadId[i]]++;
    }

    uint32_t running = 0;
    for (size_t s = 0; s < squadCount; ++s) {
        squads.memberStart[s] = running;
        squads.memberCount[s] = countsScratch[s];
        running += countsScratch[s];
    }

    members.assign(running, 0u);
    cursorScratch.assign(squadCount, 0u);
    for (size_t i = 0; i < soldiers.count; ++i) {
        if (soldiers.state[i] == SoldierState::Dead) continue;
        const uint16_t s = soldiers.squadId[i];
        members[squads.memberStart[s] + cursorScratch[s]++] = (uint32_t)i;
    }

    // Order each squad's range by previous slotIndex. Keys are NOT unique
    // within a squad -- every soldier spawns with slotIndex 0, so on the
    // first rebuild an entire squad ties. std::sort is introsort, not a
    // stable sort: on an all-equal range its output permutation is
    // implementation-defined, and libstdc++ and MSVC STL do not agree on it.
    // Tie-breaking on the soldier index (unique by construction) makes the
    // comparator a strict total order, so the sorted permutation is the same
    // on every platform regardless of algorithm.
    for (size_t s = 0; s < squadCount; ++s) {
        const uint32_t start = squads.memberStart[s];
        const uint32_t n = squads.memberCount[s];
        std::sort(members.begin() + start, members.begin() + start + n,
                  [&soldiers](uint32_t a, uint32_t b) {
                      if (soldiers.slotIndex[a] != soldiers.slotIndex[b]) {
                          return soldiers.slotIndex[a] < soldiers.slotIndex[b];
                      }
                      return a < b;
                  });
        for (uint32_t k = 0; k < n; ++k) {
            soldiers.slotIndex[members[start + k]] = (uint16_t)k;
        }
    }
}

void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t s) {
    const uint32_t start = squads.memberStart[s];
    const uint32_t n = squads.memberCount[s];

    // An emptied squad keeps its last centroid. Squads are never destroyed
    // (that is what keeps targetSquad valid without a liveness check), so a
    // wiped-out squad must not poison the field with NaN.
    if (n > 0) {
        // Summed in member order on one thread, so the result is
        // bit-reproducible regardless of worker count.
        float sumX = 0.0f, sumY = 0.0f;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t i = members[start + k];
            sumX += soldiers.posX[i];
            sumY += soldiers.posY[i];
        }
        squads.centroidX[s] = sumX / (float)n;
        squads.centroidY[s] = sumY / (float)n;
    }

    // Facing is derived from the order's objective (spec 6.6), but NOT here:
    // this function runs in phase 2, parallel across squads, while every
    // squad's own centroid above is still being written by its own
    // concurrent job. Reading another squad's centroid at that point would
    // race with that squad's write. selectTargetSquad below does the same
    // derivation safely, in phase 3, after phase 2's barrier has made every
    // centroid read-only for the rest of the tick.
    normalizeFacing(squads, s);
}

void chooseTacticalObjective(const TerrainField& terrain, const SquadHot& squads,
                             size_t s, Vec2& outObjective, Vec2& outMove) {
    const Vec2 C{ squads.centroidX[s], squads.centroidY[s] };
    const Vec2 F{ squads.facingX[s], squads.facingY[s] };
    const uint16_t tgt = squads.targetSquad[s];

    auto setHold = [&]() { outObjective = C; outMove = F; };

    // No live enemy squad: hold in place, keep current facing.
    if (tgt >= squads.count || squads.memberCount[tgt] == 0) { setHold(); return; }

    const Vec2 T{ squads.centroidX[tgt], squads.centroidY[tgt] };
    const Vec2 toT = normalizeSafe({ T.x - C.x, T.y - C.y }, F);

    const float halfExtent = squadHalfExtent(squads, s);
    const float lead = kAdvanceLead;
    const float unitRange = kUnitStats[(int)squads.unitType[s]].range; // 0 for melee
    const bool isArcher = (unitRange > 0.0f);
    const bool isCavalry = (squads.unitType[s] == UnitType::Cavalry);

    std::vector<Vec2> cands;
    cands.reserve(48);

    // Candidate 0: direct advance. If the lane C->T is clear this should win
    // (design §7.4: terrain awareness must not make every squad scenic-route).
    cands.push_back({ C.x + toT.x * lead, C.y + toT.y * lead });

    // Candidates 1,2: perpendicular flank anchors relative to the own->target lane.
    const Vec2 perpL{ -toT.y, toT.x };
    const Vec2 perpR{  toT.y, -toT.x };
    cands.push_back({ C.x + perpL.x * kFlankDistance, C.y + perpL.y * kFlankDistance });
    cands.push_back({ C.x + perpR.x * kFlankDistance, C.y + perpR.y * kFlankDistance });

    // Building corner anchors: only buildings the direct lane actually grazes
    // (expanded by standoff + our formation footprint) contribute, bounding the
    // candidate count (design §6.2). Four corners each, expanded the same way.
    const float bMargin = kObstacleStandoff + halfExtent;
    int bAdded = 0;
    for (const auto& b : terrain.buildings) {
        if (bAdded >= 12) break;
        if (!terrain.laneNearBuilding(C, T, b, bMargin)) continue;
        const float r = kObstacleStandoff + halfExtent;
        const Vec2 corners[4] = {
            { b.x - r,            b.y - r },
            { b.x + b.width + r,  b.y - r },
            { b.x - r,            b.y + b.height + r },
            { b.x + b.width + r,  b.y + b.height + r },
        };
        for (const auto& c : corners) { cands.push_back(terrain.clearOfObstacles(c)); bAdded++; }
    }

    // Tree anchors: near/far sides of trees the lane grazes (design §6.3).
    int tAdded = 0;
    for (const auto& tr : terrain.trees) {
        if (tAdded >= 12) break;
        if (!terrain.laneNearTree(C, T, tr, kObstacleStandoff + halfExtent)) continue;
        const Vec2 onLane = closestOnSegment(C, T, { tr.x, tr.y });
        const Vec2 toTree = normalizeSafe({ tr.x - onLane.x, tr.y - onLane.y }, perpL);
        const float off = tr.radius + kObstacleStandoff + halfExtent + 6.0f;
        cands.push_back(terrain.clearOfObstacles({ tr.x + toTree.x * off, tr.y + toTree.y * off }));
        cands.push_back(terrain.clearOfObstacles({ tr.x - toTree.x * off, tr.y - toTree.y * off }));
        tAdded += 2;
    }

    // Archer standoff points: beside the lane, within weapon range of the
    // target, so the squad can shoot instead of marching into a wall (§6.4).
    if (isArcher) {
        const float stand = unitRange * 0.85f;
        const Vec2 stand1 = { T.x - toT.x * stand + perpL.x * 0.25f * stand,
                              T.y - toT.y * stand + perpL.y * 0.25f * stand };
        const Vec2 stand2 = { T.x - toT.x * stand + perpR.x * 0.25f * stand,
                              T.y - toT.y * stand + perpR.y * 0.25f * stand };
        cands.push_back(terrain.clearOfObstacles(stand1));
        cands.push_back(terrain.clearOfObstacles(stand2));
    }

    // Final fallback: hold at the current centroid.
    cands.push_back(C);

    // Score every candidate; ties resolve to the earliest (stable candidate
    // order) via strict >, so the result is deterministic and platform-stable.
    const Vec2 curObj{ squads.objectiveX[s], squads.objectiveY[s] };
    float best = -1e30f;
    int bestIdx = 0;
    for (size_t i = 0; i < cands.size(); ++i) {
        const Vec2 p = cands[i];
        const Vec2 rel = { p.x - C.x, p.y - C.y };
        const float dist = std::sqrt(rel.x * rel.x + rel.y * rel.y);
        const float clr = terrain.clearanceAt(p);

        float score = 0.0f;
        score -= kDistanceWeight * dist;                                  // distanceCost
        if (terrain.segmentBlocked(C, p)) score -= kBlockedLanePenalty;    // own->anchor blocked
        if (terrain.segmentBlocked(p, T)) score -= kBlockedLanePenalty;    // anchor->target blocked
        score += kClearanceWeight * std::min(clr, kClearanceCap);         // clearanceBonus
        const float room = (clr >= halfExtent) ? halfExtent : clr;        // formationRoomBonus
        score += kFormationRoomWeight * room;
        const Vec2 dir = normalizeSafe(rel, toT);
        score += kTargetPressureWeight * (dir.x * toT.x + dir.y * toT.y); // targetPressureScore
        const float hdist = std::sqrt((p.x - curObj.x) * (p.x - curObj.x) +
                                      (p.y - curObj.y) * (p.y - curObj.y));
        score -= kHysteresisWeight * hdist;                              // currentObjectiveHysteresis

        if (isArcher) {
            const float dT = std::sqrt((p.x - T.x) * (p.x - T.x) + (p.y - T.y) * (p.y - T.y));
            if (dT <= unitRange) score += kArcherStandoffWeight;          // can shoot the target
            else                 score -= 0.5f * kArcherStandoffWeight;
            if (terrain.segmentBlocked(p, T)) score -= kArcherStandoffWeight; // blocked line to target
            const float side = std::abs(rel.x * toT.y - rel.y * toT.x);  // unitPreferenceBonus: side
            score += 0.05f * side;
        } else if (isCavalry) {
            if (clr < halfExtent + 20.0f) score -= kCavalryTightPenalty;  // tight terrain
        }
        // Infantry: no extra preference (design §6.4).

        if (score > best) { best = score; bestIdx = (int)i; }
    }

    outObjective = terrain.clearOfObstacles(cands[bestIdx]);
    const Vec2 mv = { outObjective.x - C.x, outObjective.y - C.y };
    outMove = normalizeSafe(mv, toT);
}

Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians) {
    const Vec2 cur = normalizeSafe(current, Vec2{ 1.0f, 0.0f });
    // A degenerate desired direction means "no opinion", so hold current
    // facing rather than inventing one. This is the near-coincident-centroid
    // case, and holding is exactly the right answer for it.
    const float wantLen = std::sqrt(desired.x * desired.x + desired.y * desired.y);
    if (wantLen < 1e-6f) return cur;
    const Vec2 want{ desired.x / wantLen, desired.y / wantLen };

    const float dot   = cur.x * want.x + cur.y * want.y;
    const float cross = cur.x * want.y - cur.y * want.x;

    // detmath, not libm: facing rotates every formation slot and therefore
    // reaches the state digest. cos(t) is sin(t + pi/2).
    const float c  = detmath::sin(maxRadians + detmath::HALF_PI);
    const float sn = detmath::sin(maxRadians);

    // dot >= cos(step) means the angle between them is at most `step`, so we
    // can arrive this tick. Snapping here rather than always rotating is what
    // stops a settled squad jittering around its target facing forever.
    if (dot >= c) return want;

    // Rotate by `step` in the direction of the cross product's sign. At
    // exactly 180 degrees the cross product is zero and this picks
    // counter-clockwise, arbitrarily but deterministically, which is all that
    // matters: both directions are equally short.
    const float sgn = (cross >= 0.0f) ? 1.0f : -1.0f;
    const float ss = sn * sgn;
    return Vec2{ cur.x * c - cur.y * ss, cur.x * ss + cur.y * c };
}

void selectTargetSquad(SquadHot& squads, size_t s, const TerrainField& terrain, float dt) {
    if (squads.memberCount[s] == 0) return;

    float bestDistSq = 1e30f;
    uint16_t best = squads.targetSquad[s];
    bool found = false;

    // Walked in ascending index order so ties resolve identically on every
    // thread and platform.
    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;
        if (d < bestDistSq) {
            bestDistSq = d;
            best = (uint16_t)e;
            found = true;
        }
    }

    if (found) {
        squads.targetSquad[s] = best;
        squads.order[s] = (uint8_t)SquadOrder::Advance;

        // Spec 6.6: facing comes from the order's objective, not from
        // averaging soldier directions (noisy for a loose formation) and not
        // from centroid velocity (undefined when stationary). Safe here,
        // unlike in updateSquadAggregate above: phase 3 runs after phase 2's
        // barrier, so every squad's centroid -- including the target's -- is
        // finalized and read-only for the rest of the tick, and this writes
        // only squad s's own facing.
        // Facing is SLEWED rather than assigned. See kFacingSlewRate for why
        // the rate limit is a correctness guard and not just polish: a
        // near-zero centroid-to-centroid vector flips sign on tiny numeric
        // changes, and snapping to it snaps every formation slot with it.
        const float dx = squads.centroidX[best] - squads.centroidX[s];
        const float dy = squads.centroidY[best] - squads.centroidY[s];
        const Vec2 f = slewFacing(Vec2{ squads.facingX[s], squads.facingY[s] },
                                  Vec2{ dx, dy }, kFacingSlewRate * dt);
        squads.facingX[s] = f.x;
        squads.facingY[s] = f.y;

        // Terrain-aware tactical objective + movement direction (design §7).
        // facing still points at the enemy (above); objectiveX/Y says where
        // terrain says we should stand, and moveX/Y is the lead direction so
        // the formation can side-step into clear ground while facing the foe.
        Vec2 obj{}, mv{};
        chooseTacticalObjective(terrain, squads, s, obj, mv);
        squads.objectiveX[s] = obj.x;
        squads.objectiveY[s] = obj.y;
        squads.moveX[s] = mv.x;
        squads.moveY[s] = mv.y;
    } else {
        // Every enemy squad is wiped out. Hold rather than advancing on a
        // stale target; keep the last facing.
        squads.order[s] = (uint8_t)SquadOrder::Hold;
        squads.objectiveX[s] = squads.centroidX[s];
        squads.objectiveY[s] = squads.centroidY[s];
        squads.moveX[s] = squads.facingX[s];
        squads.moveY[s] = squads.facingY[s];
    }
}

void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t s) {
    squads.targetSoldier[s] = UINT32_MAX;

    const float range = kUnitStats[(int)squads.unitType[s]].range;
    if (range <= 0.0f) return;  // melee units acquire their own targets

    const uint16_t t = squads.targetSquad[s];
    if (t >= squads.count || squads.memberCount[t] == 0) return;
    // targetSquad initialises to 0 for every squad (SquadHot::spawn), and
    // phase 2 (which calls this) runs before phase 3 (selectTargetSquad,
    // which corrects it). On a squad's very first decide, targetSquad is
    // still that spawn default, so without this check a team A squad other
    // than squad 0 would acquire a same-team targetSoldier from squad 0
    // whenever it happened to be in range -- and spawnArrows would then
    // shoot it at its own side.
    if (squads.team[t] == squads.team[s]) return;

    const float cx = squads.centroidX[s];
    const float cy = squads.centroidY[s];
    const float rangeSq = range * range;

    // members is ordered by slotIndex within each squad, so the first member in
    // range is the lowest-slotIndex one. No sort or comparison needed.
    const uint32_t start = squads.memberStart[t];
    for (uint32_t k = 0; k < squads.memberCount[t]; ++k) {
        const uint32_t idx = members[start + k];
        const float dx = soldiers.posX[idx] - cx;
        const float dy = soldiers.posY[idx] - cy;
        if (dx * dx + dy * dy <= rangeSq) {
            squads.targetSoldier[s] = idx;
            return;
        }
    }
}

void normalizeFacing(SquadHot& squads, size_t s) {
    const float fx = squads.facingX[s];
    const float fy = squads.facingY[s];
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len > 1e-6f) {
        squads.facingX[s] = fx / len;
        squads.facingY[s] = fy / len;
    } else {
        squads.facingX[s] = 1.0f;
        squads.facingY[s] = 0.0f;
    }
}
