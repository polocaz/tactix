#pragma once
#include "Units.hpp"
#include <cstdint>
#include <vector>

struct SquadHot;

// Steadiness of a unit type, 0..1. Scales both resistance to morale loss and
// recovery rate, so a disciplined unit is genuinely different in kind rather
// than merely slower to break.
float disciplineForUnit(UnitType u);

// Resolution step 5 (design 6). Updates every squad's morale from the
// casualties recorded this tick, the officer-death flag, and the rear-arc
// threat flag phase 4 wrote.
//
// Serial: this runs inside phaseResolution, the only place cross-agent state
// is touched. It reads `casualties` and `officerDied` positionally by squad
// index, so both MUST be sized to squads.count by the caller.
void updateMorale(SquadHot& squads,
                  const std::vector<uint32_t>& casualties,
                  const std::vector<uint8_t>& officerDied,
                  float dt);

// Rout entry and exit (design 6). Applied here in resolution rather than in
// the squad scorer, so a breaking squad does not wait for its next decide.
//
// Entry is immediate on crossing the threshold. Exit needs BOTH a morale
// recovery past kRallyThreshold and kRallyDuration seconds clear of enemies,
// tracked in rallyTimer. Without an explicit exit, Rout is terminal and a
// routed squad runs off the map.
void applyRoutTransitions(SquadHot& squads, float dt);
