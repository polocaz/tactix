#pragma once

class Simulation;

// What the viewer wants to see. Owned by main.cpp (the ImGui "View" section
// writes it), read by the renderer. Kept as plain data so the draw code has
// no opinion about where the toggles came from.
struct ViewSettings {
    bool  grid           = false;
    bool  squadOverlay   = true;   // squad mass discs, morale rings, facing
    bool  objectiveLines = false;  // where each squad is trying to stand
    bool  shadows        = true;
    bool  decals         = true;   // blood left where men fell
    bool  vignette       = true;
    bool  healthPips     = true;   // only drawn once zoomed in far enough
    float zoom           = 1.0f;   // camera zoom, drives level of detail
};

// One-time setup: builds the procedural ground texture for this world size.
// Must be called after InitWindow and before the first drawSimulation.
void renderInit(const Simulation& sim);
void renderShutdown();

// Drops every transient effect (decals, flashes). Call whenever the
// simulation is reset, or last battle's blood stays on the new field.
void renderResetEffects();

// Advances decals and impact flashes, and drains the simulation's death log
// into new ones. Call once per frame, before drawing.
void renderUpdateEffects(Simulation& sim, float dtSeconds);

// Draws the world. alpha is the interpolation factor in [0,1] between the
// previous and current tick's positions. Call inside BeginMode2D.
void drawSimulation(const Simulation& sim, float alpha, const ViewSettings& view);

// Screen-space overlay: army strength, casualties, clock, control hints.
// Call after EndMode2D, before the ImGui pass.
void drawHud(const Simulation& sim, const ViewSettings& view,
             float viewportWidth, float timeScale, bool paused, float battleSeconds);
