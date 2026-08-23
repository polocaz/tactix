#pragma once
#include "Units.hpp"
#include <cstdint>

// Weapon, armor and shield are ORTHOGONAL to UnitType. UnitType stays the
// coarse role the army tier reasons about (who screens, who flanks, who
// shoots); these say what a man is actually carrying.
//
// Appended, never renumbered or deleted: these values reach the state digest
// through SoldierHot::troopClass, so changing one silently invalidates every
// committed baseline.
enum class WeaponClass : uint8_t { Sword = 0, Spear = 1, Bow = 2, Javelin = 3, Lance = 4 };
enum class ArmorClass  : uint8_t { None = 0, Padded = 1, Mail = 2, Plate = 3 };
enum class ShieldClass : uint8_t { None = 0, Buckler = 1, Round = 2, Tower = 3 };
enum class TroopClass  : uint8_t {
    Levy = 0, Legionary = 1, Hoplite = 2, Huscarl = 3,
    Archer = 4, Skirmisher = 5, Knight = 6
};

constexpr uint32_t kWeaponCount = 5;
constexpr uint32_t kArmorCount  = 4;
constexpr uint32_t kShieldCount = 4;
constexpr uint32_t kTroopCount  = 7;

struct Loadout {
    UnitType    unit;
    WeaponClass weapon;
    WeaponClass sidearm;   // what he fights with once the throwing is done
    ArmorClass  armor;
    ShieldClass shield;
    float       discipline;
    uint8_t     maxHealth;
};

// Discipline lives HERE and not in three per-UnitType constants, because
// steadiness is a property of who the men are rather than of what role they
// fill. A levy spearman and a hoplite are both Infantry and are not remotely
// the same troops.
//
// The sidearm column exists for exactly one behavior: a legionary throws his
// pilum once and then fights with a sword. Every other troop's sidearm equals
// its weapon, so the throw is a table read rather than a branch.
//
// Levy, Huscarl and Skirmisher are deliberately NOT used by the default
// deployment (Simulation.cpp's troopClassForSquad), which fields Legionary
// against Hoplite so the default battle demonstrates the relief tier against
// the phalanx. They are complete, tested presets waiting on a composition UI,
// not an oversight.
constexpr Loadout kTroopLoadout[kTroopCount] = {
    /* Levy       */ { UnitType::Infantry, WeaponClass::Spear,   WeaponClass::Spear, ArmorClass::Padded, ShieldClass::Round,   0.55f, 3 },
    /* Legionary  */ { UnitType::Infantry, WeaponClass::Javelin, WeaponClass::Sword, ArmorClass::Mail,   ShieldClass::Tower,   0.90f, 3 },
    /* Hoplite    */ { UnitType::Infantry, WeaponClass::Spear,   WeaponClass::Spear, ArmorClass::Mail,   ShieldClass::Round,   0.85f, 3 },
    /* Huscarl    */ { UnitType::Infantry, WeaponClass::Sword,   WeaponClass::Sword, ArmorClass::Mail,   ShieldClass::Round,   0.88f, 3 },
    /* Archer     */ { UnitType::Archer,   WeaponClass::Bow,     WeaponClass::Sword, ArmorClass::Padded, ShieldClass::None,    0.60f, 2 },
    /* Skirmisher */ { UnitType::Archer,   WeaponClass::Javelin, WeaponClass::Sword, ArmorClass::None,   ShieldClass::Buckler, 0.50f, 2 },
    /* Knight     */ { UnitType::Cavalry,  WeaponClass::Lance,   WeaponClass::Sword, ArmorClass::Plate,  ShieldClass::Round,   0.70f, 3 },
};

constexpr const Loadout& loadoutOf(TroopClass t) {
    return kTroopLoadout[(uint32_t)t < kTroopCount ? (uint32_t)t : 0u];
}

// Overload taking the raw byte, because SoldierHot and SquadHot store the
// class as uint8_t (the SoA arrays hold plain bytes, not enums).
constexpr const Loadout& loadoutOf(uint8_t t) {
    return kTroopLoadout[(uint32_t)t < kTroopCount ? (uint32_t)t : 0u];
}

// Chance in percent that a landed blow wounds. Geometry decides whether a man
// is STRUCK; this decides whether it goes through what he is wearing.
//
// Three entries carry the design's intent and are worth naming. Javelin
// against mail (60) is the pilum: mediocre against bare flesh, the best thing
// here against armor. Bow against plate (12) is what keeps archery a
// formation-breaking and morale weapon rather than a knight-killer. Spear
// against plate (30) beating sword against plate (20) is the point
// concentrating force, and is why a spear is worth carrying even setting the
// phalanx aside.
constexpr uint8_t kWoundChancePct[kWeaponCount][kArmorCount] = {
    /*             None  Padded  Mail  Plate */
    /* Sword   */ {  85,     70,   40,    20 },
    /* Spear   */ {  80,     65,   45,    30 },
    /* Bow     */ {  75,     55,   30,    12 },
    /* Javelin */ {  85,     75,   60,    40 },
    /* Lance   */ {  95,     90,   75,    55 },
};

// Plate has to cost something or it is not a choice. Applied through the
// speedScale parameter steerToward already accepts.
constexpr float kArmorSpeedScale[kArmorCount] = { 1.00f, 0.97f, 0.92f, 0.86f };
