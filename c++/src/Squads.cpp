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

// How many friendly squads sit near enough to `point` to be caught by a volley
// aimed at it. Squad-centroid granularity on purpose: an archer squad decides
// where to shoot as a unit, and a per-soldier test would give a formation that
// disagreed with itself about where to be.
//
// Archers are excluded: they are neither a screen to shoot past nor the men a
// volley is likely to land among, since they stand well back.
uint32_t friendlySquadsNear(const SquadHot& q, size_t self, Vec2 point, float radius) {
    uint32_t n = 0;
    const float rSq = radius * radius;
    for (size_t o = 0; o < q.count; ++o) {
        if (o == self) continue;
        if (q.team[o] != q.team[self]) continue;
        if (q.memberCount[o] == 0) continue;
        if (q.unitType[o] == UnitType::Archer) continue;
        const float dx = q.centroidX[o] - point.x;
        const float dy = q.centroidY[o] - point.y;
        if (dx * dx + dy * dy <= rSq) n++;
    }
    return n;
}

// Whether a friendly melee squad stands inside the corridor from `from` to
// `to`, which is what "we have infantry between us and them" means.
bool friendlyScreenBetween(const SquadHot& q, size_t self, Vec2 from, Vec2 to) {
    for (size_t o = 0; o < q.count; ++o) {
        if (o == self) continue;
        if (q.team[o] != q.team[self]) continue;
        if (q.memberCount[o] == 0) continue;
        if (q.unitType[o] == UnitType::Archer) continue;
        const Vec2 c{ q.centroidX[o], q.centroidY[o] };
        const Vec2 onLane = closestOnSegment(from, to, c);
        const float dx = c.x - onLane.x;
        const float dy = c.y - onLane.y;
        if (dx * dx + dy * dy <= kScreenCorridorHalfWidth * kScreenCorridorHalfWidth) {
            return true;
        }
    }
    return false;
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
                             size_t s, Vec2 roleAnchor,
                             Vec2& outObjective, Vec2& outMove) {
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

    // Candidate 0: the role's own anchor (design 7.5). This was the direct
    // advance point; now the ROLE says where we want to be and terrain gets to
    // argue. A clear lane still wins, because a clear anchor scores best, so
    // §7.4 still holds: terrain awareness must not make every squad
    // scenic-route.
    (void)lead;
    cands.push_back(terrain.clearOfObstacles(roleAnchor));

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
    if (squads.role[s] == (uint8_t)SquadRole::Shoot) {
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

        if (squads.role[s] == (uint8_t)SquadRole::Shoot) {
            const float dT = std::sqrt((p.x - T.x) * (p.x - T.x) + (p.y - T.y) * (p.y - T.y));
            if (dT <= unitRange) score += kArcherStandoffWeight;          // can shoot the target
            else                 score -= 0.5f * kArcherStandoffWeight;
            if (terrain.segmentBlocked(p, T)) score -= kArcherStandoffWeight; // blocked line to target

            // Stand behind our own line where we can. The arc (design 8.1) is
            // what makes this safe rather than suicidal: a friendly screen
            // directly in front is UNDER the arrows, not in their way.
            if (friendlyScreenBetween(squads, s, p, T)) score += kScreenBonusWeight;

            // And do not stand somewhere whose impact zone is full of our own
            // men. Positioning controls what is in front of you; this term is
            // the half of the problem positioning can address at all.
            score -= kFriendlyFireWeight
                   * (float)friendlySquadsNear(squads, s, T, kMeleeMixRadius);

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

Vec2 roleAnchorFor(const SquadHot& squads, const ArmyHot& armies, size_t s) {
    const Vec2 C{ squads.centroidX[s], squads.centroidY[s] };
    const Vec2 F{ squads.facingX[s], squads.facingY[s] };
    const uint16_t tgt = squads.targetSquad[s];

    const size_t army = (size_t)squads.team[s];
    const Vec2 front = (army < armies.count)
                     ? Vec2{ armies.frontX[army], armies.frontY[army] } : C;
    const Vec2 frontDir = (army < armies.count)
                        ? Vec2{ armies.frontDirX[army], armies.frontDirY[army] } : F;

    if (tgt >= squads.count || squads.memberCount[tgt] == 0) return C;
    const Vec2 T{ squads.centroidX[tgt], squads.centroidY[tgt] };
    const Vec2 toT = normalizeSafe({ T.x - C.x, T.y - C.y }, F);

    // Withdraw and Rout are NOT handled here. They are resolved in squadDecide
    // before the terrain scorer runs, because a fleeing squad must bypass that
    // scorer entirely: its target-pressure and archer-standoff terms both
    // reward closing on the enemy, and they will happily drag an escape
    // objective back toward the thing the squad is running from. Measured, a
    // withdrawing squad's objective came out 123px from its threat while the
    // squad itself stood 170px away. See fleeObjective in squadDecide.

    switch ((SquadRole)squads.role[s]) {
        case SquadRole::Line:
            // Straight at the assigned enemy. Contact and the anchor latch are
            // what stop this from becoming a walk-through.
            return Vec2{ C.x + toT.x * kAdvanceLead, C.y + toT.y * kAdvanceLead };

        case SquadRole::Screen: {
            const uint16_t ward = squads.wardSquad[s];
            if (ward >= squads.count) {
                return Vec2{ C.x + toT.x * kAdvanceLead, C.y + toT.y * kAdvanceLead };
            }
            const Vec2 W{ squads.centroidX[ward], squads.centroidY[ward] };
            const Vec2 wardToThreat = normalizeSafe({ T.x - W.x, T.y - W.y }, toT);
            // Stand off from the WARD along the line to the threat. Being
            // between them is the whole job, so the anchor is defined relative
            // to the ward rather than to ourselves.
            return Vec2{ W.x + wardToThreat.x * kScreenStandoff,
                         W.y + wardToThreat.y * kScreenStandoff };
        }

        case SquadRole::Flank: {
            // Approach the target from its side rather than its face. Both
            // perpendiculars are equally valid; pick the nearer one so cavalry
            // do not cross the whole field, and break the tie on the left.
            const Vec2 perpL{ -toT.y, toT.x };
            const Vec2 perpR{  toT.y, -toT.x };
            const Vec2 candL{ T.x + perpL.x * kFlankSweep, T.y + perpL.y * kFlankSweep };
            const Vec2 candR{ T.x + perpR.x * kFlankSweep, T.y + perpR.y * kFlankSweep };
            const float dL = (candL.x - C.x) * (candL.x - C.x) + (candL.y - C.y) * (candL.y - C.y);
            const float dR = (candR.x - C.x) * (candR.x - C.x) + (candR.y - C.y) * (candR.y - C.y);
            return (dR < dL) ? candR : candL;
        }

        case SquadRole::Shoot: {
            const float range = kUnitStats[(int)squads.unitType[s]].range;
            // Stand off inside range but not at its edge, so a target that
            // shuffles does not immediately walk out of reach.
            return Vec2{ T.x - toT.x * range * 0.85f, T.y - toT.y * range * 0.85f };
        }

        case SquadRole::Reserve:
        default:
            // Behind the army's own line, which is exactly what the front line
            // field exists to make expressible.
            return Vec2{ front.x - frontDir.x * kReserveDepth,
                         front.y - frontDir.y * kReserveDepth };
    }
}

bool formationAvailable(TroopClass troop, FormationShape shape) {
    const Loadout& lo = loadoutOf(troop);
    switch (shape) {
        case FormationShape::Testudo:    return lo.shield == ShieldClass::Tower;
        case FormationShape::Phalanx:    return lo.weapon  == WeaponClass::Spear
                                             || lo.sidearm == WeaponClass::Spear;
        case FormationShape::Shieldwall: return lo.shield >= ShieldClass::Round;
        case FormationShape::Manipular:  return troop == TroopClass::Legionary;
        default:                         return true;
    }
}

FormationShape chooseFormation(const SquadHot& squads, size_t s) {
    const TroopClass troop = (TroopClass)squads.troopClass[s];
    const Loadout& lo = loadoutOf(troop);

    // 1. Broken men do not keep ranks, and neither do badly disciplined ones
    //    once the fighting reaches them.
    if (squads.order[s] == (uint8_t)SquadOrder::Rout) return FormationShape::Mob;
    if (squads.contact[s] && lo.discipline < kMobDisciplineFloor) {
        return FormationShape::Mob;
    }

    // 2. Under fire and not yet in melee: close up, if the shields allow it.
    //    Ordered above the fighting shapes but below the mob, and gated on NOT
    //    being in contact, because a squad with an enemy in its face has a more
    //    pressing problem than the arrows.
    if (squads.missilePressure[s] >= kTestudoThreshold && !squads.contact[s]
        && formationAvailable(troop, FormationShape::Testudo)) {
        return FormationShape::Testudo;
    }

    // 3. Fighting, or about to be. Best shape the equipment supports.
    if (squads.contact[s] || squads.nearestEnemyDist[s] < kImminentContactDist) {
        if (formationAvailable(troop, FormationShape::Phalanx))    return FormationShape::Phalanx;
        if (formationAvailable(troop, FormationShape::Shieldwall)) return FormationShape::Shieldwall;
        return FormationShape::Line;
    }

    // 4. Marching.
    if (formationAvailable(troop, FormationShape::Manipular)) return FormationShape::Manipular;
    return shapeForUnit(lo.unit);
}

void setSquadShape(SquadHot& squads, size_t s, FormationShape shape) {
    if ((FormationShape)squads.shape[s] == shape) return;
    if (squads.formationHold[s] > 0.0f) return;   // still drilling the last one

    squads.prevShape[s]     = squads.shape[s];
    squads.shape[s]         = (uint8_t)shape;
    squads.shapeBlend[s]    = kFormationChangeSeconds;
    squads.formationHold[s] = kFormationHoldSeconds;
}

void decayMissilePressure(SquadHot& squads, size_t s, float dt) {
    squads.missilePressure[s] -= kMissilePressureDecay * dt;
    if (squads.missilePressure[s] < 0.0f) squads.missilePressure[s] = 0.0f;
}

void squadDecide(SquadHot& squads, const ArmyHot& armies, size_t s,
                 const TerrainField& terrain, float dt) {
    if (squads.memberCount[s] == 0) return;

    // Timers and formation FIRST, before any of squadDecide's early returns.
    // Running them at the end would skip them on the no-enemies-left path and
    // on the withdraw bypass, and a squad that stops ticking its hold window
    // can never change shape again.
    //
    // chooseFormation therefore reads last tick's order rather than this
    // tick's. That one tick of lag is the same harmless kind friendlyNearTarget
    // already documents: squads do not teleport in 16ms.
    decayMissilePressure(squads, s, dt);
    if (squads.formationHold[s] > 0.0f) squads.formationHold[s] -= dt;
    if (squads.shapeBlend[s]    > 0.0f) squads.shapeBlend[s]    -= dt;
    setSquadShape(squads, s, chooseFormation(squads, s));

    // --- Threat survey. One walk over enemy squads feeds everything below.
    // Reading other squads' centroids is safe HERE and only here: phase 2's
    // barrier has made every centroid read-only for the rest of the tick.
    float nearestSq = 1e30f;
    uint16_t nearest = UINT16_MAX;
    float nearestMeleeSq = 1e30f;
    uint8_t rear = 0;
    const float ownFx = squads.facingX[s];
    const float ownFy = squads.facingY[s];

    for (size_t e = 0; e < squads.count; ++e) {
        if (squads.team[e] == squads.team[s]) continue;
        if (squads.memberCount[e] == 0) continue;
        const float dx = squads.centroidX[e] - squads.centroidX[s];
        const float dy = squads.centroidY[e] - squads.centroidY[s];
        const float d = dx * dx + dy * dy;

        if (d < nearestSq) { nearestSq = d; nearest = (uint16_t)e; }

        // Archers fear melee specifically, not archery: another archer squad
        // at the same distance is a duel, not a rout.
        if (squads.unitType[e] != UnitType::Archer && d < nearestMeleeSq) {
            nearestMeleeSq = d;
        }

        // Behind us and close enough to matter. A dot product against facing
        // is the whole rear-arc test: negative means the enemy is on the side
        // we are not looking at.
        if (d < kRallyRadius * kRallyRadius && (dx * ownFx + dy * ownFy) < 0.0f) {
            rear = 1;
        }
    }

    squads.nearestEnemyDist[s] = (nearestSq < 1e30f) ? std::sqrt(nearestSq) : 1e30f;
    squads.rearThreat[s] = rear;

    // Every enemy squad is wiped out. Hold rather than advancing on a stale
    // target; keep the last facing.
    if (nearest == UINT16_MAX) {
        squads.order[s] = (uint8_t)SquadOrder::Hold;
        squads.objectiveX[s] = squads.centroidX[s];
        squads.objectiveY[s] = squads.centroidY[s];
        squads.moveX[s] = squads.facingX[s];
        squads.moveY[s] = squads.facingY[s];
        return;
    }

    // targetSquad comes from the commander (Army.cpp), NOT from picking the
    // nearest enemy. That change is the whole point of the army tier. Fall
    // back to nearest only if the commander has left us pointed at a squad
    // that has since been annihilated.
    const uint16_t tgt = squads.targetSquad[s];
    if (tgt >= squads.count || squads.memberCount[tgt] == 0 ||
        squads.team[tgt] == squads.team[s]) {
        squads.targetSquad[s] = nearest;
    }

    // --- Order. Rout is owned by resolution (Morale.cpp) and must not be
    // overwritten here: a broken squad does not take orders.
    if (squads.order[s] != (uint8_t)SquadOrder::Rout) {
        const float meleeDist = (nearestMeleeSq < 1e30f)
                              ? std::sqrt(nearestMeleeSq) : 1e30f;

        if (squads.contact[s]) {
            // Contact halt (design 5.2). Overrides every role: a formation
            // that has met the enemy is fighting, whatever it was sent to do.
            squads.order[s] = (uint8_t)SquadOrder::Engaged;
        } else if ((SquadRole)squads.role[s] == SquadRole::Shoot) {
            // Panic is squad-local and evaluated EVERY tick, not on the army
            // stagger. Roles say what a squad is for; this says when it is
            // about to die, and that cannot wait up to kArmyDecideInterval
            // ticks. Same argument that puts rout in resolution.
            //
            // Entry and exit use different radii. The gap is hysteresis:
            // without it a squad sitting at the boundary flips every tick.
            //
            // The ROLE stays Shoot throughout, which is what lets a rallied
            // squad resume its job without waiting for a new assignment.
            const bool alreadyFleeing =
                (squads.order[s] == (uint8_t)SquadOrder::Withdraw);
            const float threshold = alreadyFleeing ? kArcherRallyRadius
                                                   : kArcherPanicRadius;
            squads.order[s] = (meleeDist < threshold)
                            ? (uint8_t)SquadOrder::Withdraw
                            : (uint8_t)SquadOrder::Advance;
        } else {
            switch ((SquadRole)squads.role[s]) {
                case SquadRole::Line:   squads.order[s] = (uint8_t)SquadOrder::Advance; break;
                case SquadRole::Screen: squads.order[s] = (uint8_t)SquadOrder::Screen;  break;
                case SquadRole::Flank:  squads.order[s] = (uint8_t)SquadOrder::Flank;   break;
                default:                squads.order[s] = (uint8_t)SquadOrder::Hold;    break;
            }
        }
    }

    // --- Facing, SLEWED rather than assigned. See kFacingSlewRate for why the
    // rate limit is a correctness guard and not just polish: a near-zero
    // centroid-to-centroid vector flips sign on tiny numeric changes, and
    // snapping to it snaps every formation slot with it.
    {
        const uint16_t t = squads.targetSquad[s];
        const float dx = squads.centroidX[t] - squads.centroidX[s];
        const float dy = squads.centroidY[t] - squads.centroidY[s];
        // Scaled by the formation. A phalanx at 0.35 needs about four seconds
        // to face a threat it started perpendicular to, so cavalry that gets
        // around its flank stays there. The formation's historic weakness falls
        // out of the slew mechanism that already existed rather than being a
        // special case bolted on beside it.
        const float turn = traitsOf((FormationShape)squads.shape[s]).turn;
        const Vec2 f = slewFacing(Vec2{ squads.facingX[s], squads.facingY[s] },
                                  Vec2{ dx, dy }, kFacingSlewRate * turn * dt);
        squads.facingX[s] = f.x;
        squads.facingY[s] = f.y;
    }

    // --- A fleeing squad bypasses the terrain scorer completely.
    //
    // This is not an optimisation. The scorer rewards target pressure and, for
    // a Shoot squad, standing within weapon range of the target, and both of
    // those pull an escape objective back toward the enemy: measured, a
    // withdrawing squad's objective landed 123px from its threat while the
    // squad stood 170px away. A squad that is running is not doing tactics,
    // and asking a tactical scorer where to run is the wrong question.
    if (squads.order[s] == (uint8_t)SquadOrder::Withdraw ||
        squads.order[s] == (uint8_t)SquadOrder::Rout) {
        const Vec2 C{ squads.centroidX[s], squads.centroidY[s] };
        const size_t army = (size_t)squads.team[s];
        const Vec2 frontDir = (army < armies.count)
                            ? Vec2{ armies.frontDirX[army], armies.frontDirY[army] }
                            : Vec2{ squads.facingX[s], squads.facingY[s] };

        // Away from the NEAREST enemy, not from targetSquad: an archer squad's
        // target is whoever it is shooting at, which is not necessarily the
        // melee squad bearing down on it.
        const Vec2 N{ squads.centroidX[nearest], squads.centroidY[nearest] };
        const Vec2 away = normalizeSafe({ C.x - N.x, C.y - N.y },
                                        Vec2{ -frontDir.x, -frontDir.y });
        // Biased toward our own rear, so fleeing archers run toward protection
        // rather than into a corner. The army front line is what makes "our own
        // rear" expressible at all.
        const Vec2 escape = normalizeSafe({ away.x - frontDir.x, away.y - frontDir.y },
                                          away);

        const Vec2 goal = terrain.clearOfObstacles(
            Vec2{ C.x + escape.x * kWithdrawDistance,
                  C.y + escape.y * kWithdrawDistance });
        squads.objectiveX[s] = goal.x;
        squads.objectiveY[s] = goal.y;
        squads.moveX[s] = escape.x;
        squads.moveY[s] = escape.y;
        return;
    }

    // --- Objective: the role says where, terrain gets to argue.
    const Vec2 anchor = roleAnchorFor(squads, armies, s);
    Vec2 obj{}, mv{};
    chooseTacticalObjective(terrain, squads, s, anchor, obj, mv);
    squads.objectiveX[s] = obj.x;
    squads.objectiveY[s] = obj.y;
    squads.moveX[s] = mv.x;
    squads.moveY[s] = mv.y;

    // Friendly-fire hold (design 8.3). Where you stand controls what is in
    // front of you; what you SHOOT AT controls what is around the impact, and
    // no amount of repositioning fixes a target standing in our own melee.
    //
    // Computed here in phase 4 because it reads other squads' centroids.
    // selectTargetSoldier consumes it in phase 2 of the next tick, where such
    // a read would race. See the field's declaration.
    {
        const Vec2 C2{ squads.centroidX[s], squads.centroidY[s] };
        uint16_t t2 = squads.targetSquad[s];

        auto unsafeTarget = [&](uint16_t e) {
            const Vec2 E{ squads.centroidX[e], squads.centroidY[e] };
            return friendlySquadsNear(squads, s, E, kMeleeMixRadius) > 0;
        };

        // A Shoot squad LOOKS FOR ANOTHER TARGET before giving up. Holding
        // fire is the answer only when there is no safe shot to be had, which
        // is what design 8.3's "its only in-range target" means.
        //
        // Without this the hold is far too eager: enemy squads are usually
        // engaged with our own infantry, so nearly every assigned target is
        // "mixed in" and archery switches off the moment the lines meet.
        // Measured, volleys fell about sevenfold. Re-pointing locally is the
        // same shape as the panic override: the commander says what a squad is
        // for, and the squad decides the details that cannot wait for it.
        const float wRange = kUnitStats[(int)squads.unitType[s]].range;
        if (wRange > 0.0f && t2 < squads.count && unsafeTarget(t2)) {
            for (size_t e = 0; e < squads.count; ++e) {
                if (squads.team[e] == squads.team[s]) continue;
                if (squads.memberCount[e] == 0) continue;
                const float dx = squads.centroidX[e] - C2.x;
                const float dy = squads.centroidY[e] - C2.y;
                if (dx * dx + dy * dy > wRange * wRange) continue;
                if (unsafeTarget((uint16_t)e)) continue;
                // Ascending walk, first match wins, so the choice is identical
                // on every platform and worker count.
                t2 = (uint16_t)e;
                squads.targetSquad[s] = t2;
                break;
            }
        }

        if (t2 < squads.count && squads.memberCount[t2] > 0) {
            squads.friendlyNearTarget[s] = unsafeTarget(t2) ? 1 : 0;
        } else {
            squads.friendlyNearTarget[s] = 0;
        }
    }
}

void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t s) {
    squads.targetSoldier[s] = UINT32_MAX;

    const float range = kUnitStats[(int)squads.unitType[s]].range;
    if (range <= 0.0f) return;  // melee units acquire their own targets

    // Hold fire rather than volleying into our own line. The flag was computed
    // last tick in phase 4; see its declaration for why it cannot be computed
    // here. Placed after the melee early-out so melee squads are unaffected.
    if (squads.friendlyNearTarget[s]) return;

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
