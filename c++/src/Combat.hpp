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
