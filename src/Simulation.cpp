#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>

Simulation::Simulation(int w, int h)
    : screenWidth(w), screenHeight(h) {}

void Simulation::init(size_t count) {
    agents.resize(count);

    for (auto& a : agents) {
        a.pos = {
            (float)GetRandomValue(0, screenWidth),
            (float)GetRandomValue(0, screenHeight)
        };

        // Small random jitter movement
        a.vel = {
            (float)GetRandomValue(-50, 50) / 10.0f,
            (float)GetRandomValue(-50, 50) / 10.0f
        };
    }
}

void Simulation::update(float dt) {
    for (auto& a : agents) {
        updateMovement(a, dt);
        screenWrap(a);
    }
}

void Simulation::updateMovement(Agent& a, float dt) {
    a.pos.x += a.vel.x * dt;
    a.pos.y += a.vel.y * dt;
}

void Simulation::screenWrap(Agent& a) {
    if (a.pos.x < 0) a.pos.x = screenWidth;
    if (a.pos.x > screenWidth) a.pos.x = 0;
    if (a.pos.y < 0) a.pos.y = screenHeight;
    if (a.pos.y > screenHeight) a.pos.y = 0;
}

void Simulation::draw() {
    for (auto& a : agents) {
        DrawCircleV(a.pos, 3, LIGHTGRAY);
    }
}
