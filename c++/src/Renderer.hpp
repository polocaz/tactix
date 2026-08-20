#pragma once

class Simulation;

// Draws the simulation. alpha is the interpolation factor in [0,1] between
// the previous and current tick's positions.
void drawSimulation(const Simulation& sim, float alpha);
