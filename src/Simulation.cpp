#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>
#include "spdlog/spdlog.h"

Simulation::Simulation(int w, int h)
    : screenWidth(w), screenHeight(h) {}

void Simulation::init(size_t count) {
    spdlog::info("Initializing {} agents with SoA layout", count);
    entities.reserve(count);
    prevPosX.reserve(count);
    prevPosY.reserve(count);

    // Spawn agents with random positions and velocities
    for (size_t i = 0; i < count; i++) {
        float px = (float)GetRandomValue(0, screenWidth);
        float py = (float)GetRandomValue(0, screenHeight);
        float vx = (float)GetRandomValue(-100, 100);
        float vy = (float)GetRandomValue(-100, 100);
        
        entities.spawn(px, py, vx, vy);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Calculate memory usage
    size_t memoryPerEntity = sizeof(float) * 6;  // 4 floats hot + 2 floats prev
    float totalMB = (memoryPerEntity * count) / (1024.0f * 1024.0f);
    spdlog::info("Memory usage: {:.2f} MB ({} bytes/entity)", totalMB, memoryPerEntity);
}

void Simulation::tick(float dt) {
    // Store previous positions for interpolation
    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }
    
    // Update movement (SoA style - cache friendly, Design Doc §14.2)
    updateMovement(dt);
    
    // Screen wrapping
    screenWrap();
}

void Simulation::updateMovement(float dt) {
    // SIMD-friendly: compiler auto-vectorizes this loop
    for (size_t i = 0; i < entities.count; i++) {
        entities.posX[i] += entities.velX[i] * dt;
        entities.posY[i] += entities.velY[i] * dt;
    }
}

void Simulation::screenWrap() {
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.posX[i] < 0) entities.posX[i] += w;
        if (entities.posX[i] > w) entities.posX[i] -= w;
        if (entities.posY[i] < 0) entities.posY[i] += h;
        if (entities.posY[i] > h) entities.posY[i] -= h;
    }
}

void Simulation::draw(float alpha) {
    // Interpolated rendering for smooth visuals (Design Doc §8.1)
    for (size_t i = 0; i < entities.count; i++) {
        float renderX = prevPosX[i] + (entities.posX[i] - prevPosX[i]) * alpha;
        float renderY = prevPosY[i] + (entities.posY[i] - prevPosY[i]) * alpha;
        
        DrawCircle(static_cast<int>(renderX), static_cast<int>(renderY), 2.5f, RAYWHITE);
    }
}
