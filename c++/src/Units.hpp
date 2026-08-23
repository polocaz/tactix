#pragma once
#include <cstdint>

enum class Team : uint8_t { A = 0, B = 1 };
enum class UnitType : uint8_t { Infantry = 0, Archer = 1, Cavalry = 2 };
enum class SoldierState : uint8_t { Forming = 0, Engaged = 1, Routing = 2, Dead = 3 };
// Appended, never renumbered: shape reaches the state digest through
// SquadHot::shape. Behavior for each lives in kFormationTraits (Formation.hpp).
enum class FormationShape : uint8_t {
    Line = 0, Column = 1, Wedge = 2, Loose = 3,
    Shieldwall = 4, Phalanx = 5, Testudo = 6, Manipular = 7, Mob = 8
};

// Appended, never renumbered: order feeds the state digest, so changing an
// existing value silently invalidates every committed baseline.
//
// Engaged is the contact halt (design 5.2). Withdraw and Rout differ
// deliberately: a withdrawing squad keeps its formation and rallies on
// command, a routing one does neither.
enum class SquadOrder : uint8_t {
    Hold = 0, Advance = 1, Engaged = 2, Flank = 3,
    Screen = 4, Withdraw = 5, Rout = 6
};

constexpr uint32_t kUnitTypeCount = 3;

// How far ahead of its centroid a squad aims its formation slots while
// advancing. Soldiers chase a target slightly in front of where they stand,
// which drags the centroid forward and marches the formation.
//
// This sets the army's marching speed, and it does so indirectly: the squad
// advances only as fast as its members close a gap this size, which lands well
// under the unit's nominal speed. At 6 the march was slow enough that most of
// a battle was the walk-up, and non-penetration made it worse by a third once
// bodies started resisting each other.
//
// Measured at 2000 agents, seed 42, by the tick at which the first soldier
// dies: 6 puts first contact between ticks 900 and 1200, 10 puts it between
// 600 and 900.
//
// MUST stay under kSlotSpacing (12). The lead is how far a soldier sits behind
// its slot in steady state, so a lead at or above the rank spacing means the
// formation stretches faster than its members can close it, and the ranks pull
// apart into a column instead of marching as a body.
constexpr float kAdvanceLead = 10.0f;

// How fast a squad may rotate its formation, in radians per second. Facing
// rotates every slot, so an unbounded turn teleports the whole formation.
// It is also the guard against the melee spin: two squads whose centroids
// nearly coincide produce a near-zero facing vector that flips sign on tiny
// numeric changes, and a rate limit turns that flip into a slow sweep no
// matter what the vector does. About 143 degrees per second: fast enough to
// answer a flank, slow enough to read as a maneuver.
constexpr float kFacingSlewRate = 2.5f;

struct Vec2 { float x, y; };

// Manipular geometry. The interval is not decoration: it is the corridor a
// relieved maniple retires through (see the line relief in Army.cpp), and a
// legion without it is a shieldwall with holes in it.
constexpr uint32_t kManipleWidth    = 8;      // columns before an interval
constexpr float    kManipleInterval = 12.0f;  // one kSlotSpacing

// Legion line relief. A maniple that has been fighting long enough to be spent
// retires through the interval behind it and a fresh one steps up. Every
// mechanism this needs already existed: SquadRole::Reserve, SquadOrder::Withdraw
// (which keeps formation, unlike Rout), and an army decide phase that is
// already serial.
constexpr float kReliefLossFraction    = 0.60f;  // survivors below this counts as spent
constexpr float kReliefMoraleThreshold = 0.55f;
constexpr float kReliefContactSeconds  = 20.0f;
constexpr float kReliefSearchRadius    = 260.0f;
constexpr float kReliefClearDistance   = 70.0f;  // separation at which the swap completes
constexpr float kReliefCooldownSeconds = 15.0f;

// Two ordering constraints against constants that already exist, stated here
// rather than left to be discovered:
//
// kReliefMoraleThreshold (0.55) MUST stay above kRallyThreshold (0.45), or a
// maniple routs before it is ever judged spent and the relief never fires.
//
// The lateral offset an advancing maniple aims at MUST be about half a maniple
// wide, so the two squads are never walking at the same point. That is what
// lets them swap places without any new collision logic.
//
// Written in terms of kManipleInterval rather than kSlotSpacing (which they are
// equal to) because kSlotSpacing lives in Formation.hpp, which includes this
// header rather than the other way round.
constexpr float kReliefLateralOffset = (float)kManipleWidth * kManipleInterval * 0.5f;

// The pilum. Thrown once as the lines close, then the swords come out. The
// javelin row of kWoundChancePct is deliberately the best thing in the game
// against mail and mediocre against bare flesh, which is what makes the volley
// a decision rather than a free opener.
//
// kPilumRange sits above kImminentContactDist so the throw happens while the
// squad is still closing, not after it has already locked up in melee.
constexpr float kPilumRange      = 90.0f;   // px
constexpr float kJavelinSpeed    = 260.0f;  // px/s, flatter and faster than an arrow
constexpr float kJavelinLifetime = 0.6f;    // s, enough for kPilumRange with margin

// Formation transitions. A squad caught mid-drill takes the worse cover and the
// slower speed of both shapes, so changing formation under fire is a real
// decision rather than a free upgrade.
constexpr float kFormationChangeSeconds = 1.4f;

// Minimum time in a shape before another change is allowed. This is the
// hysteresis without which a squad sitting at any threshold flips every tick,
// in the same spirit as kContactClearSeconds and the rally threshold gap.
constexpr float kFormationHoldSeconds = 3.0f;

// Discipline below which a squad in contact stops being a formation at all.
constexpr float kMobDisciplineFloor = 0.45f;

// How close an enemy must be before a squad adopts its fighting shape rather
// than its marching one. Squads close at roughly 45px/s, so 60px is a little
// over a second of warning, which is under kFormationChangeSeconds: a squad
// that waits for contact is still drilling when the enemy arrives.
constexpr float kImminentContactDist = 60.0f;

// Missile pressure. Each strike on a member adds, and it bleeds off every tick.
// Costs nothing to compute: resolution already walks every arrow that hit
// someone. With these three, roughly two hits a second sustained closes a
// tower-shield squad into a testudo.
constexpr float kMissilePressurePerHit = 0.30f;
constexpr float kMissilePressureDecay  = 0.60f;   // per second
constexpr float kTestudoThreshold      = 1.00f;

// How far a mob's slots scatter from their grid position. MUST stay under
// kSeparationRadius (10): a scatter at or above it puts two slots close enough
// that separation shoves their occupants apart, and the formation spends the
// battle fighting its own avoidance force. Same failure mode kSeparationRadius
// documents against kSlotSpacing.
constexpr float kMobJitter = 4.0f;

// Radius of a soldier's own neighbour query. Archer range deliberately
// exceeds it, which is why target assignment lives on the squad.
constexpr float kSeekRadius = 150.0f;

// Health, discipline and equipment moved to kTroopLoadout (Loadout.hpp).
// Speed and range stayed here, and the split is not arbitrary: these two are
// genuinely properties of the ROLE. A mounted man is fast because he is
// mounted, and a bow reaches 280px whoever is holding it.
struct UnitStats {
    float   speed;      // px/s
    float   range;      // px, 0 means melee only
};

constexpr UnitStats kUnitStats[kUnitTypeCount] = {
    /* Infantry */ { 45.0f,   0.0f },
    /* Archer   */ { 42.0f, 280.0f },
    /* Cavalry  */ { 95.0f,   0.0f },
};

// Shared by Simulation.cpp (deployment) and Soldiers.cpp (Task 8) so both
// agree on which formation shape a unit type marches in.
constexpr FormationShape shapeForUnit(UnitType u) {
    switch (u) {
        case UnitType::Archer:  return FormationShape::Loose;
        case UnitType::Cavalry: return FormationShape::Wedge;
        default:                return FormationShape::Line;
    }
}

// Arrow flight. Speed is deliberately modest: a faster arrow crosses more
// ground per tick, and the swept hit test in task 8 is what keeps that honest.
constexpr float   kArrowSpeed      = 200.0f;  // px/s
constexpr float   kArrowLifetime   = 3.0f;    // seconds before it falls short
constexpr uint8_t kArrowDamage     = 1;
constexpr float   kSoldierRadius   = 4.0f;    // for hit tests

// Melee. Reach is deliberately close to kSlotSpacing so that two formations
// have to actually touch before anyone swings.
constexpr float   kMeleeReach    = 14.0f;  // px
constexpr uint8_t kMeleeDamage   = 1;
constexpr float   kMeleeCooldown = 0.8f;   // seconds between swings

// Contact detection (design 5.1). kContactRadius MUST sit below kMeleeReach,
// and the margin is load-bearing rather than cosmetic.
//
// The halt freezes the formation anchor where the squad stands, so whatever
// distance the front ranks are apart at that moment is the distance they stay
// apart. Setting this ABOVE kMeleeReach therefore does not "register contact
// just before the first blow": it stops the line permanently out of swinging
// range and no one ever fights. Measured directly at kMeleeReach * 1.4, a
// 2000-agent battle produced zero deaths in 700 ticks, against a first death
// at tick 448 before the halt existed.
//
// Below reach, the halt latches a line that is already fighting. 0.85 leaves
// room for the men to jostle without any of them falling out of reach, and
// sits comfortably above the 8px floor non-penetration enforces.
constexpr float kContactRadius = kMeleeReach * 0.85f;   // 11.9px

// Fraction of the front rank that must have an enemy in reach. A single
// over-eager skirmisher must not halt a whole formation, and requiring the
// whole rank would never fire on a ragged line.
constexpr float kContactFraction = 0.25f;

// Grace period before contact is allowed to clear. Without it a squad
// flickers between Engaged and Advance every time a front-rank duel ends.
constexpr float kContactClearSeconds = 0.75f;

// How long the formation anchor takes to ease back onto the live centroid
// after contact clears. Snapping instead would teleport the whole formation
// by however far the centroid drifted during the fight.
constexpr float kAnchorReleaseSeconds = 0.5f;

// Rank-depth compression (design 5.4). Cohesion is discipline * morale, and
// it scales how deep a formation stands: an organized unit keeps full rank
// spacing, a shaken one collapses toward its front rank. Only DEPTH is
// scaled, never width, because a formation that narrowed under pressure would
// read as a funnel rather than as a crowd.
constexpr float kMinCompression = 0.45f;

// Additional squeeze while engaged. Men press forward into a fight. Ranks stay
// ranks: compression scales every rank's depth uniformly and never reorders
// slots, so rank order is preserved at any value.
constexpr float kContactCompression = 0.8f;

// The simulation is fixed-step by design (see the determinism contract): tick
// is always called with this value, and the benchmark and tests all use it.
// Named here so resolution-phase code that has no dt parameter can still
// express rates per second rather than per tick.
constexpr float kFixedTimestep = 1.0f / 60.0f;

// Morale (design 6). All tuning knobs; the existence of each input is not.
//
// Morale falls from casualties taken this tick as a fraction of squad size,
// from the officer dying, and from an enemy in the rear arc. It recovers on a
// base rate. Discipline scales BOTH resistance to loss and recovery rate,
// which is what makes a disciplined unit meaningfully different rather than
// just slower to break.
constexpr float kMoraleLossPerCasualtyFraction = 2.0f;
constexpr float kMoraleOfficerDeathPenalty     = 0.15f;
constexpr float kMoraleRearThreatPerSecond     = 0.12f;
constexpr float kMoraleRecoveryPerSecond       = 0.05f;

// Rout thresholds. routThreshold scales DOWN with discipline, so a
// disciplined squad holds at a morale a levy would have broken at.
// kRallyThreshold sits above the worst-case rout threshold: the gap is
// hysteresis, and without it a squad at the boundary oscillates every tick.
constexpr float kBaseRoutThreshold = 0.30f;
constexpr float kRallyThreshold    = 0.45f;
constexpr float kRallyRadius       = 220.0f;
constexpr float kRallyDuration     = 3.0f;    // seconds clear of enemies

// Discipline used to live here as three per-UnitType constants. It is now a
// column of kTroopLoadout (Loadout.hpp), because steadiness is a property of
// who the men are rather than of what role they fill: a levy spearman and a
// hoplite are both Infantry and are not remotely the same troops.

// How close an enemy melee squad must be before an archer squad is judged to
// need a bodyguard. Deliberately larger than kArcherPanicRadius (design 8.5):
// the screen should already be in place by the time the archers would panic.
constexpr float kScreenThreatRadius = 400.0f;

// Ceiling on how much of the infantry may be assigned to bodyguard duty.
// The per-archer-squad cap does not bound this: archers are roughly a quarter
// of an army, and on a crowded field nearly every archer squad has an enemy
// inside kScreenThreatRadius, so one guard each still claims nearly every
// infantry squad. Measured at 10,000 agents, that left 82 squads screening
// and 3 holding the line.
constexpr float kMaxScreenFraction = 0.34f;

// How often each army re-decides roles, in ticks. The two armies are offset by
// team so they never decide on the same tick, and an assignment persists long
// enough to be legible rather than churning every frame.
constexpr uint32_t kArmyDecideInterval = 30u;

// Role anchor geometry (design 7.3). All tuning knobs.
constexpr float kScreenStandoff = 45.0f;   // how far in front of its ward a screen stands
constexpr float kFlankSweep     = 90.0f;   // how wide of the target cavalry swing
constexpr float kReserveDepth   = 120.0f;  // how far behind the front line reserves wait

// Combat weight per man, used by the army tier to size how much force an
// enemy squad demands. Cavalry hit hardest per head, archers least in a
// stand-up fight, so an equal head count is not an equal threat.
constexpr float kStrengthPerMan[kUnitTypeCount] = {
    /* Infantry */ 1.0f,
    /* Archer   */ 0.7f,
    /* Cavalry  */ 1.6f,
};

// Shot accuracy. Spread is carried in integer milliradians because Rng::range
// is integer-only; passing float bounds to it does not compile.
constexpr int   kArrowBaseSpreadMrad = 40;    // about 2.3 degrees at rest
constexpr float kArcherCooldown      = 1.5f;  // seconds between shots

// kArrowHitChancePct, a flat 45 percent for every arrow against every man,
// used to live here. It is REPLACED, not supplemented, by the staged model in
// Loadout.hpp and Shields.hpp: geometry decides whether an arrow crosses a man,
// his shield may block it, and kWoundChancePct decides whether it gets through
// what he is wearing. Multiplying a flat 45 percent by those two would put a
// bowman near 8 percent against a shielded, mailed man and make archery
// ornamental. RngUse::ArrowHitRoll survives in the enum, unused, because
// deleting an enumerator reshuffles every value after it.

// Fraction of the flight to the target that an arrow spends above head
// height. Below this it hits nothing at all, friend or foe.
//
// This is what resolves a contradiction in the requirements. "Put infantry
// between yourself and the target" and "avoid friendly fire" are opposites
// under a flat trajectory, because your own screen is exactly what you would
// be shooting through. Under an arc they are consistent, for the same reason
// they were in reality: massed archery was indirect, so the danger to your own
// side came from where the arrows landed, not from where they were loosed.
//
// Two consequences that are correct rather than bugs. A point-blank shot has a
// tiny liveAfter and so is live almost immediately, which is right: close
// range archery is direct fire. And an arrow that MISSES stays live for the
// rest of its flight, so a long overshoot can still strike whatever is behind
// the target, on either side.
constexpr float kArrowArcFraction = 0.6f;

// Below this speed a soldier counts as standing still.
constexpr float kWalkSpeed = 6.0f;   // px/s

// How long an archer must be settled before it shoots at full accuracy.
constexpr float kSteadyTime = 0.8f;  // seconds

// Extra spread multiplier while unsettled, on top of the existing speed term.
// This is the number that makes a squad which keeps repositioning keep
// missing, and therefore the number that makes archers choose to hold still.
// Without it, advancing forever is free and no archer ever has a reason to
// stop, which is exactly the behaviour this feature exists to change.
constexpr float kUnsettledSpreadMultiplier = 2.5f;

// An archer cannot loose at a target more than this far off its own movement
// direction while moving faster than a walk. Expressed as a cosine because
// that is what a dot product against a normalized heading gives directly.
//
// This is the whole of "cannot fire backward while fleeing", with no state
// check: flight points away from the enemy, so a fleeing archer's target is
// always behind it. An archer sidestepping slowly into position is under
// kWalkSpeed and unaffected, so it can still loose sideways.
constexpr float kMaxFireCos = 0.5f;   // 60 degrees

// Archer flight (design 8.5). The panic radius is deliberately SMALLER than
// kScreenThreatRadius: the screen should already be in place by the time the
// archers would break for the rear. The gap between panic and rally is
// hysteresis, without which a squad at the boundary flips every tick.
constexpr float kArcherPanicRadius = 170.0f;
constexpr float kArcherRallyRadius = 280.0f;

// Archers drop their discipline and run. Faster than their marching speed,
// and faster than the infantry chasing them, or fleeing would be pointless.
constexpr float kFleeSpeedMultiplier = 1.45f;

// How far back a withdrawing squad aims, measured from its own position along
// the escape direction.
constexpr float kWithdrawDistance = 200.0f;

// Archer positioning scorer terms (design 8.2).
constexpr float kScreenBonusWeight       = 30.0f;  // reward standing behind our own line
constexpr float kScreenCorridorHalfWidth = 60.0f;  // how wide the "behind them" corridor is
constexpr float kFriendlyFireWeight      = 45.0f;  // penalty per friendly squad near the impact
constexpr float kMeleeMixRadius          = 70.0f;  // how close to the target counts as mixed in
