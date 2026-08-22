#pragma once
#include <cstdint>

enum class Team : uint8_t { A = 0, B = 1 };
enum class UnitType : uint8_t { Infantry = 0, Archer = 1, Cavalry = 2 };
enum class SoldierState : uint8_t { Forming = 0, Engaged = 1, Routing = 2, Dead = 3 };
enum class FormationShape : uint8_t { Line = 0, Column = 1, Wedge = 2, Loose = 3 };

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
// which drags the centroid forward and marches the formation. Kept well under
// kSlotSpacing so the formation does not stretch faster than soldiers close it.
constexpr float kAdvanceLead = 6.0f;

// How fast a squad may rotate its formation, in radians per second. Facing
// rotates every slot, so an unbounded turn teleports the whole formation.
// It is also the guard against the melee spin: two squads whose centroids
// nearly coincide produce a near-zero facing vector that flips sign on tiny
// numeric changes, and a rate limit turns that flip into a slow sweep no
// matter what the vector does. About 143 degrees per second: fast enough to
// answer a flank, slow enough to read as a maneuver.
constexpr float kFacingSlewRate = 2.5f;

struct Vec2 { float x, y; };

// Radius of a soldier's own neighbour query. Archer range deliberately
// exceeds it, which is why target assignment lives on the squad.
constexpr float kSeekRadius = 150.0f;

struct UnitStats {
    float   speed;      // px/s
    float   range;      // px, 0 means melee only
    uint8_t maxHealth;
};

constexpr UnitStats kUnitStats[kUnitTypeCount] = {
    /* Infantry */ { 45.0f,   0.0f, 3 },
    /* Archer   */ { 42.0f, 280.0f, 2 },
    /* Cavalry  */ { 95.0f,   0.0f, 3 },
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

// Discipline by unit type. Cavalry are the least steady, archers are fragile
// but not undisciplined, infantry are the anchor. Constant per type for now:
// per-squad variation is a tuning knob nobody has asked for yet.
constexpr float kDisciplineInfantry = 0.85f;
constexpr float kDisciplineArcher   = 0.60f;
constexpr float kDisciplineCavalry  = 0.70f;

// How close an enemy melee squad must be before an archer squad is judged to
// need a bodyguard. Deliberately larger than kArcherPanicRadius (design 8.5):
// the screen should already be in place by the time the archers would panic.
constexpr float kScreenThreatRadius = 400.0f;

// How often each army re-decides roles, in ticks. The two armies are offset by
// team so they never decide on the same tick, and an assignment persists long
// enough to be legible rather than churning every frame.
constexpr uint32_t kArmyDecideInterval = 30u;

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

// An arrow whose flight path crosses a soldier still has to get through
// shield, mail and luck. Geometry decides whether a shot comes CLOSE; this
// decides whether it lands. Tuning knob: lower it for a grindier, melee-led
// battle, raise it to make archery decisive.
constexpr int kArrowHitChancePct = 45;
