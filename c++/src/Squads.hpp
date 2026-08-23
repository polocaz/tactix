#pragma once
#include "Units.hpp"
#include "Loadout.hpp"
#include "Terrain.hpp"
#include "Army.hpp"
#include <cstdint>
#include <vector>

struct SoldierHot;  // defined in Simulation.hpp

struct SquadHot {
    std::vector<Team>     team;
    std::vector<UnitType> unitType;
    // A squad is uniform, so this duplicates its members' value. Deliberate:
    // squadDecide reads it every tick, and reaching into a member soldier for
    // a value that cannot differ across the squad would be an indirection into
    // a different array for nothing.
    std::vector<uint8_t>  troopClass;
    // The formation this squad is standing in RIGHT NOW. Previously derived
    // from unitType through shapeForUnit on every read, which made a formation
    // a permanent property of a unit type. Per-squad state is what lets a squad
    // change shape mid-battle.
    std::vector<uint8_t>  shape;
    // Formation transition state. shapeBlend counts DOWN the seconds remaining
    // in a change, and while it runs the squad has the WORSE of the two shapes
    // in every respect that matters. Written from the formation-choice commit
    // onward; declared here because shieldBlockPct already has to read them to
    // charge that cost.
    std::vector<uint8_t>  prevShape;
    std::vector<float>    shapeBlend;
    std::vector<float>    centroidX, centroidY;
    std::vector<float>    facingX, facingY;
    std::vector<uint8_t>  order;          // plan 3 gives this meaning
    std::vector<uint16_t> targetSquad;
    // Written every tick by selectTargetSoldier (Simulation::phaseSquadAggregate,
    // phase 2, parallel across squads), from the previous tick's targetSquad.
    // UINT32_MAX when the squad has no ranged weapon or no target in range.
    std::vector<uint32_t> targetSoldier;
    std::vector<float>    morale;         // plan 3
    std::vector<float>    discipline;     // plan 3
    std::vector<uint32_t> memberStart, memberCount;

    // Terrain-aware tactical objective (design §7.1): the point this squad's
    // centroid should aim for, chosen from candidate anchors around obstacles.
    // Separate from targetSquad ("who we care about") and from facingX/Y
    // ("where we point", usually the enemy centroid). objectiveX/Y is the
    // WHERE TERRAIN SAYS WE STAND; moveX/Y is its normalized direction, used
    // for the advance lead so a squad can side-step into clear ground while
    // still facing the enemy (§7.2). Both written only by this squad in phase
    // 3, so they preserve the thread-count-invariance rule.
    std::vector<float>    objectiveX, objectiveY;
    std::vector<float>    moveX, moveY;

    // Contact state (design 5.1). `contact` is 1 while this squad's front rank
    // is engaged; `contactTimer` counts DOWN the grace period before contact
    // is allowed to clear, so a squad does not flicker between engaged and
    // advancing as individual enemies die.
    std::vector<uint8_t> contact;
    std::vector<float>   contactTimer;

    // Formation origin (design 5.2). This, NOT the centroid, is what
    // slotWorldPosition builds slots from. While the squad is free it tracks
    // the centroid exactly, reproducing the pre-contact behaviour; on the
    // rising edge of contact it latches, which is what cuts the centroid/slot
    // feedback loop that made melee a rotating blob.
    std::vector<float> anchorX, anchorY;

    // Counts DOWN the post-contact ease, and is the reason free tracking and
    // release are separate states rather than one blended rule. Easing every
    // tick regardless would leave a MARCHING squad's anchor permanently
    // trailing its centroid by speed * dt / easeRate, about 22px at infantry
    // pace, dragging the whole formation backward. A free squad must track
    // exactly; only a squad that has just disengaged eases.
    std::vector<float> anchorReleaseTimer;

    // Morale inputs, written in phase 4 by the squad decide (which may read
    // every squad's centroid, because phase 2's barrier has made them
    // read-only) and consumed in serial resolution by Morale.cpp.
    //
    // Both are computed inside the enemy loop selectTargetSquad ALREADY walks,
    // so they cost nothing asymptotically. Computing them in resolution
    // instead would be O(squads squared) on the serial path every tick.
    std::vector<uint8_t> rearThreat;        // an enemy squad sits behind us
    std::vector<float>   nearestEnemyDist;  // to the closest live enemy squad

    // Counts UP the time spent clear of enemies while routing. Rally needs
    // sustained safety, not an instant of it.
    std::vector<float>   rallyTimer;

    // Commander assignment (design 7.3). Written only by phaseArmyDecide,
    // read by the squad decide. wardSquad is UINT16_MAX when this squad holds
    // no Screen assignment.
    std::vector<uint8_t>  role;
    std::vector<uint16_t> wardSquad;

    // Set when this squad's target sits in a melee containing our own men, so
    // shooting at it would drop arrows on friends (design 8.3).
    //
    // Written in phase 4, where reading other squads' centroids is safe, and
    // read by selectTargetSoldier in phase 2 of the NEXT tick, where it is
    // not. That one tick of lag is the price of the phase ordering and is
    // harmless: squads do not teleport in 16ms. Do NOT "fix" it by moving the
    // computation into selectTargetSoldier -- that reintroduces a real data
    // race which is benign at one worker thread and so passes every test.
    std::vector<uint8_t> friendlyNearTarget;

    size_t count = 0;

    void spawn(Team t, UnitType u) {
        team.push_back(t);
        unitType.push_back(u);
        troopClass.push_back((uint8_t)TroopClass::Levy);
        shape.push_back((uint8_t)FormationShape::Line);
        prevShape.push_back((uint8_t)FormationShape::Line);
        shapeBlend.push_back(0.0f);
        centroidX.push_back(0.0f);
        centroidY.push_back(0.0f);
        facingX.push_back(1.0f);
        facingY.push_back(0.0f);
        order.push_back(0);
        targetSquad.push_back(0);
        targetSoldier.push_back(UINT32_MAX);
        morale.push_back(1.0f);
        discipline.push_back(1.0f);
        memberStart.push_back(0);
        memberCount.push_back(0);
        objectiveX.push_back(0.0f);
        objectiveY.push_back(0.0f);
        moveX.push_back(1.0f);
        moveY.push_back(0.0f);
        contact.push_back(0);
        contactTimer.push_back(0.0f);
        anchorX.push_back(0.0f);
        anchorY.push_back(0.0f);
        anchorReleaseTimer.push_back(0.0f);
        rearThreat.push_back(0);
        nearestEnemyDist.push_back(1e30f);
        rallyTimer.push_back(0.0f);
        role.push_back(0);                 // SquadRole::Reserve
        wardSquad.push_back(UINT16_MAX);
        friendlyNearTarget.push_back(0);
        count++;
    }

    // Empties every array and resets the count. Lives HERE, next to spawn(),
    // and not as a hand-written list in Simulation::reset, because those two
    // lists have to agree field for field and nothing checks that they do.
    //
    // They did not agree: reset() set count to 0 while leaving the newer
    // arrays populated, so the next init() pushed onto them and every new
    // field came out offset by the previous run's count, silently reading the
    // last battle's values. Adding a field is now one edit here instead of a
    // memory-corruption bug waiting on someone remembering a second list.
    void clear() {
        team.clear();
        unitType.clear();
        troopClass.clear();
        shape.clear();
        prevShape.clear();
        shapeBlend.clear();
        centroidX.clear();
        centroidY.clear();
        facingX.clear();
        facingY.clear();
        order.clear();
        targetSquad.clear();
        targetSoldier.clear();
        morale.clear();
        discipline.clear();
        memberStart.clear();
        memberCount.clear();
        objectiveX.clear();
        objectiveY.clear();
        moveX.clear();
        moveY.clear();
        contact.clear();
        contactTimer.clear();
        anchorX.clear();
        anchorY.clear();
        anchorReleaseTimer.clear();
        rearThreat.clear();
        nearestEnemyDist.clear();
        rallyTimer.clear();
        role.clear();
        wardSquad.clear();
        friendlyNearTarget.clear();
        count = 0;
    }
};

// Regroups `members` by squad, ordering each squad's range by the soldiers'
// previous slotIndex, then reassigns slotIndex densely from zero.
//
// Ordering by previous slotIndex rather than array position is a correctness
// requirement: it is what makes a dead officer's successor the soldier who
// was standing next to them, instead of an arbitrary survivor whose position
// would teleport the formation's anchor.
//
// countsScratch/cursorScratch are caller-owned scratch buffers, not output:
// this runs once a tick in serial resolution, and owning them lets the
// caller keep their capacity across ticks instead of paying two heap
// allocations every tick.
void rebuildSquadMembers(SoldierHot& soldiers, SquadHot& squads,
                         std::vector<uint32_t>& members,
                         std::vector<uint32_t>& countsScratch,
                         std::vector<uint32_t>& cursorScratch);

// Recomputes each squad's centroid from its members. Parallel-safe: writes
// only the squad it is given, reads only that squad's members.
void updateSquadAggregate(const SoldierHot& soldiers, SquadHot& squads,
                          const std::vector<uint32_t>& members,
                          size_t squadIndex);

// Turns the commander's role into an order, a facing, and an objective for
// squad `s`. Replaces selectTargetSquad: targets now come from the army tier
// (Army.cpp), so this no longer chooses one.
//
// Parallel-safe in phase 4, and ONLY in phase 4: it reads every squad's
// centroid, which phase 2's barrier has made read-only for the rest of the
// tick. It writes only squad `s`.
void squadDecide(SquadHot& squads, const ArmyHot& armies, size_t squadIndex,
                 const TerrainField& terrain, float dt);

// Where this squad's role says it wants to stand, before terrain has an
// opinion. chooseTacticalObjective scores candidates AROUND this point, so
// this is what makes terrain awareness compose with coordination rather than
// override it.
Vec2 roleAnchorFor(const SquadHot& squads, const ArmyHot& armies, size_t squadIndex);

// Rotates `current` toward `desired` by at most `maxRadians`, returning a unit
// vector. Falls back to `current` (normalized) when `desired` is degenerate,
// and to (1,0) when both are, so this never produces NaN.
//
// Deliberately avoids atan2: there is no deterministic atan2 in DetMath, and
// none is needed. The dot product answers "are we within one step" and the
// cross product answers "which way", which is the whole decision.
Vec2 slewFacing(Vec2 current, Vec2 desired, float maxRadians);

// Chooses a terrain-aware tactical objective for squad `s` (design §6/§7) and
// writes it to outObjective/outMove. Reads only squad `s` and its target's
// centroids plus `terrain`; writes nothing else, so it is parallel-safe inside
// phase 3. Deterministic: candidate order is fixed and the scorer breaks ties
// by candidate index (design §7.3). Direct lane clear -> direct objective; a
// blocked lane -> a clear anchor around the obstacle (§7.4).
void chooseTacticalObjective(const TerrainField& terrain, const SquadHot& squads,
                             size_t squadIndex, Vec2 roleAnchor,
                             Vec2& outObjective, Vec2& outMove);

// Spec 6.5. Picks the member of targetSquad with the LOWEST slotIndex that is
// within weapon range of our centroid, or UINT32_MAX if none is.
//
// Recomputed every tick and never cached across ticks: compaction renumbers
// soldiers, so a stored soldier index is stale the moment anyone dies.
//
// Lowest slotIndex is what makes officers preferentially targeted without a
// special case, since the officer is whoever holds slot 0.
void selectTargetSoldier(const SoldierHot& soldiers, SquadHot& squads,
                         const std::vector<uint32_t>& members, size_t squadIndex);

// Normalizes a squad's facing vector in place, falling back to (1, 0) for a
// zero-length input rather than producing NaN. Shared by deployment
// (Simulation::init, which must establish the unit-length invariant slot
// rotation depends on) and updateSquadAggregate (which maintains it every
// tick), so both routes go through one definition of "unit length."
void normalizeFacing(SquadHot& squads, size_t squadIndex);
