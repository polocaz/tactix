#pragma once
#include <cstdint>

enum class Team : uint8_t { A = 0, B = 1 };
enum class UnitType : uint8_t { Infantry = 0, Archer = 1, Cavalry = 2 };
enum class SoldierState : uint8_t { Forming = 0, Engaged = 1, Routing = 2, Dead = 3 };
enum class FormationShape : uint8_t { Line = 0, Column = 1, Wedge = 2, Loose = 3 };

// Plan 3 adds FlankLeft, FlankRight, Charge, Withdraw, and Rout. Values are
// part of the state digest, so append new ones rather than renumbering.
enum class SquadOrder : uint8_t { Hold = 0, Advance = 1 };

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

// Contact detection (design 5.1). kContactRadius sits deliberately ABOVE
// kMeleeReach so a squad registers contact just BEFORE its front rank can
// swing: halting on the same frame as the first blow would let the formation
// overrun by a stride first.
constexpr float kContactRadius = kMeleeReach * 1.4f;   // 19.6px

// Fraction of the front rank that must have an enemy in reach. A single
// over-eager skirmisher must not halt a whole formation, and requiring the
// whole rank would never fire on a ragged line.
constexpr float kContactFraction = 0.25f;

// Grace period before contact is allowed to clear. Without it a squad
// flickers between Engaged and Advance every time a front-rank duel ends.
constexpr float kContactClearSeconds = 0.75f;

// Shot accuracy. Spread is carried in integer milliradians because Rng::range
// is integer-only; passing float bounds to it does not compile.
constexpr int   kArrowBaseSpreadMrad = 40;    // about 2.3 degrees at rest
constexpr float kArcherCooldown      = 1.5f;  // seconds between shots

// An arrow whose flight path crosses a soldier still has to get through
// shield, mail and luck. Geometry decides whether a shot comes CLOSE; this
// decides whether it lands. Tuning knob: lower it for a grindier, melee-led
// battle, raise it to make archery decisive.
constexpr int kArrowHitChancePct = 45;
