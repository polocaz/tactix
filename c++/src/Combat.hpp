#pragma once
#include <cstdint>
#include <vector>

struct SoldierHot;
class SpatialHash;

// Chooses an enemy within kMeleeReach and records it in the soldier's own
// intentTarget. Parallel-safe: writes only soldierIndex's own slot, and reads
// only positions and teams, which no parallel phase writes.
//
// Ties are broken by lowest soldier index, so the choice is identical at any
// thread count and on any platform.
void selectMeleeTarget(SoldierHot& soldiers, const SpatialHash& hash,
                       size_t soldierIndex, std::vector<uint32_t>& scratch);

// Resolution step 1 (spec 5.5). Applies every soldier's melee intent in
// ascending soldier index order. Single-threaded: this is the only place a
// soldier may write another soldier's health.
void applyMeleeIntents(SoldierHot& soldiers);

// Resolution step 4 (spec 5.5). Runs BEFORE compaction, because the officer is
// identified by slotIndex 0 and compaction reassigns slots.
void recordCasualties(SoldierHot& soldiers,
                      std::vector<uint32_t>& casualties,
                      std::vector<uint8_t>& officerDied);

// Resolution step 6 (spec 5.5). Swap-with-back removal of Dead soldiers. This
// INVALIDATES every soldier index, so it must run after every step that reads
// one.
void compactDead(SoldierHot& soldiers,
                 std::vector<float>& prevPosX,
                 std::vector<float>& prevPosY);
